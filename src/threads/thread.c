#include "threads/thread.h"
#include "devices/timer.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

int f = 1 << 14; // For fixed point arithmetic
// System load average in 17.14 fixed point number representation.
static int load_avg;

static bool thread_inited = false;

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

/* List of all processes.  Processes are added to this list
   when they are first scheduled and removed when they exit. */
static struct list all_list;

/* List of processes that are waiting on a timer tick before
   they wake up next */
static struct list sleep_list;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Stack frame for kernel_thread(). */
struct kernel_thread_frame 
  {
    void *eip;                  /* Return address. */
    thread_func *function;      /* Function to call. */
    void *aux;                  /* Auxiliary data for function. */
  };

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *running_thread (void);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static bool is_thread (struct thread *) UNUSED;
static void *alloc_frame (struct thread *, size_t size);
static void schedule (void);
void thread_schedule_tail (struct thread *prev);
static tid_t allocate_tid (void);

/* Helper functions */
void other_thread_set_priority(struct thread *other, int priority);
void thread_set_priority_main(struct thread *other, int priority, bool donated);
static bool thread_less_func(const struct list_elem *l, const struct list_elem *r, void *aux);

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void
thread_init (void) 
{
  ASSERT (intr_get_level () == INTR_OFF);
  load_avg = 0;

  lock_init (&tid_lock);
  list_init (&ready_list);
  list_init (&all_list);
  list_init (&sleep_list);

  /* Set up a thread structure for the running thread. */
  initial_thread = running_thread ();
  init_thread (initial_thread, "main", PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid ();
  initial_thread->sleep_end = 0;

  thread_inited = true;
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void) 
{
  /* Create the idle thread. */
  struct semaphore idle_started;
  sema_init (&idle_started, 0);
  thread_create ("idle", PRI_MIN, idle, &idle_started);

  /* Start preemptive thread scheduling. */
  intr_enable ();

  /* Wait for the idle thread to initialize idle_thread. */
  sema_down (&idle_started);
}

void
thread_sleep (int64_t end) 
{
    struct thread *t = thread_current(); 
    ASSERT (!intr_context ());

    t->sleep_end = end;
    list_push_back(&sleep_list, &t->sleep_elem);
}

void update_mlfq_priority() {
  struct list_elem *e;

  ASSERT (intr_get_level () == INTR_OFF);

  for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, allelem);

      int fpa_pri = (PRI_MAX * f) - (t->recent_cpu / 4) - (t->nice * f * 2);
      t->mlfq_priority = fpa_pri / f;
      if(t->mlfq_priority > PRI_MAX) { t->mlfq_priority = PRI_MAX; }
      if(t->mlfq_priority < PRI_MIN) { t->mlfq_priority = PRI_MIN; }

      //printf("\n%d %d\n", t->tid, t->mlfq_priority);
    }
}

void update_mlfq_priority_cur() {

  ASSERT (intr_get_level () == INTR_OFF);

  struct thread *t = thread_current(); 

  int fpa_pri = (PRI_MAX * f) - (t->recent_cpu / 4) - (t->nice * f * 2);
  t->mlfq_priority = fpa_pri / f;
  if(t->mlfq_priority > PRI_MAX) { t->mlfq_priority = PRI_MAX; }
  if(t->mlfq_priority < PRI_MIN) { t->mlfq_priority = PRI_MIN; }
}

void update_recent_cpu() {
  struct list_elem *e;

  ASSERT (intr_get_level () == INTR_OFF);

  for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, allelem);

      int coeff = 2*load_avg;
      int den = 2*load_avg + (1*f);
      coeff = ((int64_t) coeff) * f / den;
      t->recent_cpu = (((int64_t) coeff) * t->recent_cpu / f) + (t->nice * f);
    }
}

void update_load_avg() {
  int num_rdy_threads = list_size(&ready_list);
  if(thread_current() != idle_thread) { num_rdy_threads = num_rdy_threads + 1; }

  //printf("\nnrt %d la %d tt %d\n", num_rdy_threads, thread_get_load_avg(), timer_ticks());
  num_rdy_threads = num_rdy_threads * f; // Convert to fpa
  num_rdy_threads = num_rdy_threads / 60; // For the load average

  // Calculate coefficient on load_avg
  int coeff = 59 * f;
  coeff = coeff / 60;
  // Multiply the coefficient
  int temp = ((int64_t) load_avg) * coeff / f;

  // Now add the two values together
  load_avg = temp + num_rdy_threads;
}


/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (int64_t cur_ticks) 
{
  struct thread *t = thread_current ();

  if(thread_mlfqs && thread_inited) {
    // Update system-wide load average and recent cpu counts, if necessary
    if(cur_ticks % TIMER_FREQ == 0) {
      update_load_avg();
      update_recent_cpu();
      update_mlfq_priority();

    } else {
      t->recent_cpu = t->recent_cpu + (1 * f);      
    }

    // Set thread priorities on every thread
    if(cur_ticks % 4 == 0) {
      update_mlfq_priority_cur();
    }

  }

  /* Update statistics. */
  if (t == idle_thread)
    idle_ticks++;
#ifdef USERPROG
  else if (t->pagedir != NULL)
    user_ticks++;
#endif
  else
    kernel_ticks++;

  if(thread_inited) {
    thread_wake(cur_ticks);
  }

  /* Enforce preemption. */
  if (++thread_ticks >= TIME_SLICE)
    intr_yield_on_return ();
}

/* Prints thread statistics. */
void
thread_print_stats (void) 
{
  printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
          idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t
thread_create (const char *name, int priority,
               thread_func *function, void *aux) 
{
  struct thread *t;
  struct kernel_thread_frame *kf;
  struct switch_entry_frame *ef;
  struct switch_threads_frame *sf;
  tid_t tid;

  ASSERT (function != NULL);

  /* Allocate thread. */
  t = palloc_get_page (PAL_ZERO);
  if (t == NULL)
    return TID_ERROR;

  /* Initialize thread. */
  init_thread (t, name, priority);
  tid = t->tid = allocate_tid ();

  /* Stack frame for kernel_thread(). */
  kf = alloc_frame (t, sizeof *kf);
  kf->eip = NULL;
  kf->function = function;
  kf->aux = aux;

  /* Stack frame for switch_entry(). */
  ef = alloc_frame (t, sizeof *ef);
  ef->eip = (void (*) (void)) kernel_thread;

  /* Stack frame for switch_threads(). */
  sf = alloc_frame (t, sizeof *sf);
  sf->eip = switch_entry;
  sf->ebp = 0;

  /* Add to run queue. */
  thread_unblock (t);

  /* Yield immediately if the newly created thread has a higher
   * priority. */
  if (!thread_mlfqs && priority > thread_current()->priority) {
    thread_yield();
  }

  return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) 
{
  ASSERT (!intr_context ());
  ASSERT (intr_get_level () == INTR_OFF);

  thread_current ()->status = THREAD_BLOCKED;
  schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void
thread_unblock (struct thread *t) 
{
  enum intr_level old_level;

  ASSERT (is_thread (t));

  old_level = intr_disable ();
  ASSERT (t->status == THREAD_BLOCKED);
  list_insert_ordered(&ready_list, &t->elem, thread_less_func, NULL);
  t->status = THREAD_READY;
  intr_set_level (old_level);
}

/* Returns the name of the running thread. */
const char *
thread_name (void) 
{
  return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) 
{
  struct thread *t = running_thread ();
  
  /* Make sure T is really a thread.
     If either of these assertions fire, then your thread may
     have overflowed its stack.  Each thread has less than 4 kB
     of stack, so a few big automatic arrays or moderate
     recursion can cause stack overflow. */
  ASSERT (is_thread (t));
  ASSERT (t->status == THREAD_RUNNING);

  return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) 
{
  return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) 
{
  ASSERT (!intr_context ());

#ifdef USERPROG
  process_exit ();
#endif

  /* Remove thread from all threads list, set our status to dying,
     and schedule another process.  That process will destroy us
     when it calls thread_schedule_tail(). */
  intr_disable ();
  list_remove (&thread_current()->allelem);
  thread_current ()->status = THREAD_DYING;
  schedule ();
  NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void) 
{
  struct thread *cur = thread_current ();
  enum intr_level old_level;
  
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  if (cur != idle_thread)
    /* The ready list should be ordered by priority. */
    list_insert_ordered(&ready_list, &cur->elem, thread_less_func, 
                        NULL);
  cur->status = THREAD_READY;
  schedule ();
  intr_set_level (old_level);
}

/* Invoke function 'func' on all threads, passing along 'aux'.
   This function must be called with interrupts off. */
void
thread_foreach (thread_action_func *func, void *aux)
{
  struct list_elem *e;

  ASSERT (intr_get_level () == INTR_OFF);

  for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, allelem);
      func (t, aux);
    }
}

/* Wakes all sleeping threads whose sleep_end is less than current
   ticks. Must be called with interrupts off. */
void
thread_wake (int64_t ticks)
{
  struct list_elem *e;

  ASSERT (intr_get_level () == INTR_OFF);

  e = list_begin(&sleep_list);
  while (e != list_end (&sleep_list))
    {
      struct thread *t = list_entry (e, struct thread, sleep_elem);
      ASSERT(is_thread(t));
      e = list_next(e);
        if(t->sleep_end <= ticks) {
            t->sleep_end = 0;
            list_remove(&t->sleep_elem);
            thread_unblock(t);
        } 
    }
}

/* Sets the current thread's priority to NEW_PRIORITY when a donation
   was not performed. */
void
thread_set_priority (int new_priority) 
{
  if (!thread_mlfqs) {
    thread_set_priority_main(thread_current(), new_priority, false);
  }
}

/* Sets an arbitrary thread's priority to NEW_PRIORITY when a donation
   was not performed. */
void
other_thread_set_priority(struct thread *other, int new_priority)
{
    thread_set_priority_main(other, new_priority, false);
}

/* Sets an arbitrary thread's priority to NEW_PRIORITY. */
void
thread_set_priority_main(struct thread *other, int new_priority, 
                         bool donated)
{
  if(thread_mlfqs) { return; }

  struct thread *next;

  /* If there was a donation, the current priority should change, but
   * the original priority should stay the same. */
  if (donated == true) {
    other->priority = new_priority;
  }
  else {
    /* If there wasn't a donation and the thread hasn't received a 
     * donation yet, both the current and the original priorites should
     * change. */
    if (other->priority == other->original_priority) {
      other->priority = new_priority;
      other->original_priority = new_priority;
    }
    /* If there wasn't a donation but the thread has received a 
     * donation (it has a higher priority than the original priority),
     * only the original priority should change. */
    else {
      other->original_priority = new_priority;
    }
  }

  /* If the current thread no longer has the highest priority, yield.
   * The ready list is ordered in descending order, so the thread with
   * the highest priority will be at the beginning of the list. */
  next = list_entry(list_begin(&ready_list), struct thread, elem);
  if (!thread_mlfqs && other == thread_current() && next->priority > new_priority) {
    thread_yield();
  }
  /* If we're setting the priority for a thread that's not the current 
   * thread and it's in the ready list, we need to make sure the ready 
   * list is still in order. */
  else if (other->status == THREAD_READY) {
    list_sort(&ready_list, thread_less_func, NULL);
  }
}

/* Returns the current thread's priority. */
int
thread_get_priority (void) 
{
  if(thread_mlfqs) {
    return thread_current ()->mlfq_priority;
  } else {
    return thread_current ()->priority;
  }
}

/* Yields if the current thread isn't the highest priority */
void
thread_yield_if_not_highest () {

  ASSERT(intr_get_level() == INTR_OFF);
  struct thread *t = thread_current();
  ASSERT(is_thread(t));

  // Find highest priority and see if we need to yield.
  struct list_elem *e;

  for (e = list_begin(&ready_list); e != list_end(&ready_list);
       e = list_next(e)) 
  {
    struct thread *cur = list_entry(e, struct thread, elem);

    ASSERT(is_thread(cur));

    if(cur->mlfq_priority > t->mlfq_priority) {
      thread_yield();
    }
  }

}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice) 
{
  struct thread *t = thread_current();

  enum intr_level old_level = intr_disable();

  int correct_nice = nice;
  if(correct_nice > 20) { correct_nice = 20; }
  if(correct_nice < -20) { correct_nice = -20; } 
  t->nice = correct_nice;

  // Recalculate priority

  int fpa_pri = (PRI_MAX * f) - (t->recent_cpu / 4) - (t->nice * f * 2);
  t->mlfq_priority = fpa_pri / f;
  if(t->mlfq_priority > PRI_MAX) { t->mlfq_priority = PRI_MAX; }
  if(t->mlfq_priority < PRI_MIN) { t->mlfq_priority = PRI_MIN; }

  thread_yield_if_not_highest();

  intr_set_level(old_level);

}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) 
{
  return thread_current ()->nice;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) 
{
  enum intr_level old_level;
  old_level = intr_disable();
  int ret_value =  ((load_avg * 100) + f / 2) / f;  
  intr_set_level(old_level);
  return ret_value;
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) 
{
  struct thread *t = thread_current();
  enum intr_level old_level;
  old_level = intr_disable();
  int ret_value = ((t->recent_cpu * 100) + f /2) / f;
  intr_set_level(old_level);
  return ret_value;
}
/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) 
{
  struct semaphore *idle_started = idle_started_;
  idle_thread = thread_current ();
  sema_up (idle_started);

  for (;;) 
    {
      /* Let someone else run. */
      intr_disable ();
      thread_block ();

      /* Re-enable interrupts and wait for the next one.

         The `sti' instruction disables interrupts until the
         completion of the next instruction, so these two
         instructions are executed atomically.  This atomicity is
         important; otherwise, an interrupt could be handled
         between re-enabling interrupts and waiting for the next
         one to occur, wasting as much as one clock tick worth of
         time.

         See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
         7.11.1 "HLT Instruction". */
      asm volatile ("sti; hlt" : : : "memory");
    }
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) 
{
  ASSERT (function != NULL);

  intr_enable ();       /* The scheduler runs with interrupts off. */
  function (aux);       /* Execute the thread function. */
  thread_exit ();       /* If function() returns, kill the thread. */
}

/* Returns the running thread. */
struct thread *
running_thread (void) 
{
  uint32_t *esp;

  /* Copy the CPU's stack pointer into `esp', and then round that
     down to the start of a page.  Because `struct thread' is
     always at the beginning of a page and the stack pointer is
     somewhere in the middle, this locates the curent thread. */
  asm ("mov %%esp, %0" : "=g" (esp));
  return pg_round_down (esp);
}

/* Returns true if T appears to point to a valid thread. */
static bool
is_thread (struct thread *t)
{
  return t != NULL && t->magic == THREAD_MAGIC;
}

/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority)
{
  enum intr_level old_level;

  ASSERT (t != NULL);
  ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
  ASSERT (name != NULL);

  memset (t, 0, sizeof *t);
  t->status = THREAD_BLOCKED;
  strlcpy (t->name, name, sizeof t->name);
  t->stack = (uint8_t *) t + PGSIZE;
  if (thread_mlfqs) {
    t->priority = 0;
    t->mlfq_priority = 0;
  } else {
    t->priority = priority;
  }
  t->nice = 0; // TODO figure out how to inherit from parent
  t->original_priority = priority;
  list_init(&t->locks);
#ifdef USERPROG
  list_init(&t->file_descs);
  list_init(&t->child_threads);
  list_init(&t->mapped_files);
#endif
  t->desired_lock = NULL;
  t->magic = THREAD_MAGIC;
  t->sleep_end = 0;

  old_level = intr_disable ();
  list_push_back (&all_list, &t->allelem);
  intr_set_level (old_level);
}

/* Allocates a SIZE-byte frame at the top of thread T's stack and
   returns a pointer to the frame's base. */
static void *
alloc_frame (struct thread *t, size_t size) 
{
  /* Stack data is always allocated in word-size units. */
  ASSERT (is_thread (t));
  ASSERT (size % sizeof (uint32_t) == 0);

  t->stack -= size;
  return t->stack;
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) 
{
  // NOTE FROM ILYA: This will almost certainly merge conflict, but
  // I think my version should work for your part too, so you might
  // want to consider keeping this version.

  if (list_empty (&ready_list)) {
    return idle_thread;
  }

  int highest_pri = PRI_MIN - 1;
  struct thread *max;
  struct list_elem *max_elem;
  struct list_elem *e;

  for( e = list_begin(&ready_list); e != list_end(&ready_list);
       e = list_next(e)) 
  {
    struct thread *t = list_entry(e, struct thread, elem);

    // Strictly greater than to enforce round robin on equal priorities.
    if(thread_mlfqs && t->mlfq_priority > highest_pri) {
      max = t;
      max_elem = e;
      highest_pri = t->mlfq_priority;
    }
    else if(!thread_mlfqs && t->priority > highest_pri) {
      max = t;
      max_elem = e;
      highest_pri = t->priority;
    }
  }

  list_remove(max_elem);
  return max;
}

/* Completes a thread switch by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.  This function is normally invoked by
   thread_schedule() as its final action before returning, but
   the first time a thread is scheduled it is called by
   switch_entry() (see switch.S).

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function.

   After this function and its caller returns, the thread switch
   is complete. */
void
thread_schedule_tail (struct thread *prev)
{
  struct thread *cur = running_thread ();
  
  ASSERT (intr_get_level () == INTR_OFF);

  /* Mark us as running. */
  cur->status = THREAD_RUNNING;

  /* Start new time slice. */
  thread_ticks = 0;

#ifdef USERPROG
  /* Activate the new address space. */
  process_activate ();
#endif

  /* If the thread we switched from is dying, destroy its struct
     thread.  This must happen late so that thread_exit() doesn't
     pull out the rug under itself.  (We don't free
     initial_thread because its memory was not obtained via
     palloc().) */
  if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread) 
    {
      ASSERT (prev != cur);
      palloc_free_page (prev);
    }
}

/* Schedules a new process.  At entry, interrupts must be off and
   the running process's state must have been changed from
   running to some other state.  This function finds another
   thread to run and switches to it.

   It's not safe to call printf() until thread_schedule_tail()
   has completed. */
static void
schedule (void) 
{
  struct thread *cur = running_thread ();
  struct thread *next = next_thread_to_run ();
  struct thread *prev = NULL;

  ASSERT (intr_get_level () == INTR_OFF);
  ASSERT (cur->status != THREAD_RUNNING);
  ASSERT (is_thread (next));

  if (cur != next)
    prev = switch_threads (cur, next);
  thread_schedule_tail (prev);
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) 
{
  static tid_t next_tid = 1;
  tid_t tid;

  lock_acquire (&tid_lock);
  tid = next_tid++;
  lock_release (&tid_lock);

  return tid;
}

struct thread *get_thread_from_tid(tid_t tid) {
    struct list_elem *e;
  
    for (e = list_begin(&all_list); 
         e != list_end(&all_list);
         e = list_next(e)) {
  
    struct thread *t = list_entry(e, struct thread, allelem);
        if (t->tid == tid) {
            return t;
        }
    }
  
    return NULL;
}


/* Offset of `stack' member within `struct thread'.
   Used by switch.S, which can't figure it out on its own. */
uint32_t thread_stack_ofs = offsetof (struct thread, stack);

/* Helper function passed as a parameter to list functions that
 * tell it how to compare two elements of the list. */
static bool thread_less_func(const struct list_elem *l, const struct list_elem *r, void *aux) {
  struct thread *lthread, *rthread;
  ASSERT (l != NULL && r != NULL);
  lthread = list_entry(l, struct thread, elem);
  rthread = list_entry(r, struct thread, elem);
  if(!thread_mlfqs) {
    return (lthread->priority > rthread->priority);
  }
  else {
    return (lthread->mlfq_priority > rthread->mlfq_priority);
  }
}

