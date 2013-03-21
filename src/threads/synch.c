/*! \file synch.c
 *
 * Implementation of various thread synchronization primitives.
 */

/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
*/

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/* Helper functions */
static bool thread_less_func(const struct list_elem *l, const struct list_elem *r, void *aux);
static bool lock_less_func(const struct list_elem *l, const struct list_elem *r, void *aux);
static bool sema_less_func(const struct list_elem *l, const struct list_elem *r, void *aux);

/*! Initializes semaphore SEMA to VALUE.  A semaphore is a
    nonnegative integer along with two atomic operators for
    manipulating it:

    - down or "P": wait for the value to become positive, then
      decrement it.

    - up or "V": increment the value (and wake up one waiting
      thread, if any). */
void sema_init(struct semaphore *sema, unsigned value) {
    ASSERT(sema != NULL);

    sema->value = value;
    list_init(&sema->waiters);
}

/*! Down or "P" operation on a semaphore.  Waits for SEMA's value
    to become positive and then atomically decrements it.

    This function may sleep, so it must not be called within an
    interrupt handler.  This function may be called with
    interrupts disabled, but if it sleeps then the next scheduled
    thread will probably turn interrupts back on. */
void sema_down(struct semaphore *sema) {
    enum intr_level old_level;

    ASSERT(sema != NULL);
    ASSERT(!intr_context());

    old_level = intr_disable();
    while (sema->value == 0) {
        /* Insert into the waiting list based on priority. 
         * Basically, the waiting list is now a priority queue. */
        ASSERT(list_begin(&sema->waiters) != NULL);
        list_insert_ordered(&sema->waiters, &thread_current()->elem, 
                            thread_less_func, NULL);
        thread_block();
    }
    sema->value--;
    intr_set_level(old_level);
}

/*! Down or "P" operation on a semaphore, but only if the
    semaphore is not already 0.  Returns true if the semaphore is
    decremented, false otherwise.

    This function may be called from an interrupt handler. */
bool sema_try_down(struct semaphore *sema) {
    enum intr_level old_level;
    bool success;

    ASSERT(sema != NULL);

    old_level = intr_disable();
    if (sema->value > 0) {
        sema->value--;
        success = true; 
    }
    else {
      success = false;
    }
    intr_set_level(old_level);

    return success;
}

/*! Up or "V" operation on a semaphore.  Increments SEMA's value
    and wakes up one thread of those waiting for SEMA, if any.

    This function may be called from an interrupt handler. */
void sema_up(struct semaphore *sema) {
    enum intr_level old_level;
    struct thread *other = NULL;

    ASSERT(sema != NULL);

    old_level = intr_disable();
    sema->value++;
    if (!list_empty(&sema->waiters)) {
        /* Note that I changed sema_down to insert in order by
         * priority, so this will pop the highest priority 
         * waiting process. Apparently, this isn't entirely true
         * because I had to add a list_sort() above for semaphores
         * to work. */
        list_sort(&(sema->waiters), thread_less_func, NULL);
        other = list_entry(list_pop_front(&sema->waiters),
                           struct thread, elem);
        thread_unblock(other);
        if (thread_mlfqs && other->mlfq_priority > 
                            thread_current()->mlfq_priority) {
            thread_yield();
        }
    }

    /* Check if the current thread needs to yield. */
    if (other != NULL && other->priority > thread_current()->priority && !thread_mlfqs) {
        thread_yield();
    }
    intr_set_level(old_level);
}

static void sema_test_helper(void *sema_);

/*! Self-test for semaphores that makes control "ping-pong"
    between a pair of threads.  Insert calls to printf() to see
    what's going on. */
void sema_self_test(void) {
    struct semaphore sema[2];
    int i;

    printf("Testing semaphores...");
    sema_init(&sema[0], 0);
    sema_init(&sema[1], 0);
    thread_create("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
    for (i = 0; i < 10; i++) {
        sema_up(&sema[0]);
        sema_down(&sema[1]);
    }
    printf ("done.\n");
}

/*! Thread function used by sema_self_test(). */
static void sema_test_helper(void *sema_) {
    struct semaphore *sema = sema_;
    int i;

    for (i = 0; i < 10; i++) {
        sema_down(&sema[0]);
        sema_up(&sema[1]);
    }
}

/*! Initializes LOCK.  A lock can be held by at most a single
    thread at any given time.  Our locks are not "recursive", that
    is, it is an error for the thread currently holding a lock to
    try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock. */
void lock_init(struct lock *lock) {
    ASSERT(lock != NULL);

    lock->holder = NULL;
    sema_init(&lock->semaphore, 1);
    lock->priority = PRI_MIN;
}

/*! Acquires LOCK, sleeping until it becomes available if
    necessary.  The lock must not already be held by the current
    thread.

    This function may sleep, so it must not be called within an
    interrupt handler.  This function may be called with
    interrupts disabled, but interrupts will be turned back on if
    we need to sleep. */
void lock_acquire(struct lock *lock) {
    ASSERT(lock != NULL);
    ASSERT(!intr_context());
    ASSERT(!lock_held_by_current_thread(lock));

    if(!thread_mlfqs) {
    struct thread *lock_holder = lock->holder;
    struct thread *curr = thread_current();
    struct lock *curr_lock = lock;

    /* The current thread is now waiting on the lock. */
    curr->desired_lock = lock;

    /* Set lock priority when a thread first wants to acquire the 
     * lock. */
    if (lock_holder == NULL)
        curr_lock->priority = curr->priority;
    /* Starting with the thread holding the lock the current 
     * thread wants to access, loop through threads holding the
     * lock the previous thread is waiting on and perform a
     * priority donation. */
    while (lock_holder != NULL && 
           curr->priority > lock_holder->priority) {
        /* Donate priority */
        thread_set_priority_main(lock_holder, curr->priority, 
                                 true);
        if (curr->priority > curr_lock->priority) {
            curr_lock->priority = curr->priority;
        }
        /* Go to the thread's lock's holder if it exists.
         * Otherwise, we break out of the loop. */
        if (lock_holder->desired_lock != NULL) {
            curr_lock = lock_holder->desired_lock;
            lock_holder = curr_lock->holder;
        }
        else {
            break;
        }
    }
    }

    sema_down(&lock->semaphore);
    lock->holder = thread_current();

    if(!thread_mlfqs) {
    /* Once the thread acquires the lock, it's no longer waiting 
     * on it. */
    lock->holder->desired_lock = NULL;
    /* Once the thread acquires the lock, insert it into its locks 
     * list. */
    list_insert_ordered(&(lock->holder->locks), &(lock->lockelem), 
                        lock_less_func, NULL);
    }
}

/*! Tries to acquires LOCK and returns true if successful or false
    on failure.  The lock must not already be held by the current
    thread.

    This function will not sleep, so it may be called within an
    interrupt handler. */
bool lock_try_acquire(struct lock *lock) {
    bool success;

    ASSERT(lock != NULL);
    ASSERT(!lock_held_by_current_thread(lock));

    success = sema_try_down(&lock->semaphore);
    if (success) {
      lock->holder = thread_current();
      if(!thread_mlfqs) {
      lock->holder->desired_lock = NULL;
      /* Once the thread acquires the lock, insert it into its locks 
       * list. */
      list_insert_ordered(&(lock->holder->locks), &(lock->lockelem), 
                          lock_less_func, NULL);
      }
    }

    return success;
}

/*! Releases LOCK, which must be owned by the current thread.

    An interrupt handler cannot acquire a lock, so it does not
    make sense to try to release a lock within an interrupt
    handler. */
void lock_release(struct lock *lock) {
    struct list_elem *next;
    struct lock *next_lock;

    ASSERT(lock != NULL);
    ASSERT(lock_held_by_current_thread(lock));

    struct thread *curr = thread_current();

    lock->holder = NULL;
    sema_up(&lock->semaphore);

    if(!thread_mlfqs) {
    /* Remove the lock from the thread's locks list. */
    list_remove(&(lock->lockelem));
    /* If the thread holds no more locks, then restore original
     * priority (before donation). */
    if (list_empty(&(curr->locks))) {
        thread_set_priority_main(curr, curr->original_priority, true);
    }
    /* Otherwise, the thread's priority becomes the priority of
       the highest priority lock. */
    else {
        list_sort(&(curr->locks), lock_less_func, NULL);
        next = list_front(&(curr->locks));
        next_lock = list_entry(next, struct lock, lockelem);
        thread_set_priority_main(curr, next_lock->priority, true);
    }
    }
}

/*! Returns true if the current thread holds LOCK, false
    otherwise.  (Note that testing whether some other thread holds
    a lock would be racy.) */
bool lock_held_by_current_thread(const struct lock *lock) {
    ASSERT(lock != NULL);

    return lock->holder == thread_current();
}

/*! One semaphore in a list. */
struct semaphore_elem {
    struct list_elem elem;              /*!< List element. */
    struct semaphore semaphore;         /*!< This semaphore. */
};

/*! Initializes condition variable COND.  A condition variable
    allows one piece of code to signal a condition and cooperating
    code to receive the signal and act upon it. */
void cond_init(struct condition *cond) {
    ASSERT(cond != NULL);

    list_init(&cond->waiters);
}

/*! Atomically releases LOCK and waits for COND to be signaled by
    some other piece of code.  After COND is signaled, LOCK is
    reacquired before returning.  LOCK must be held before calling
    this function.

    The monitor implemented by this function is "Mesa" style, not
    "Hoare" style, that is, sending and receiving a signal are not
    an atomic operation.  Thus, typically the caller must recheck
    the condition after the wait completes and, if necessary, wait
    again.

    A given condition variable is associated with only a single
    lock, but one lock may be associated with any number of
    condition variables.  That is, there is a one-to-many mapping
    from locks to condition variables.

    This function may sleep, so it must not be called within an
    interrupt handler.  This function may be called with
    interrupts disabled, but interrupts will be turned back on if
    we need to sleep. */
void cond_wait(struct condition *cond, struct lock *lock) {
    struct semaphore_elem waiter;

    ASSERT(cond != NULL);
    ASSERT(lock != NULL);
    ASSERT(!intr_context());
    ASSERT(lock_held_by_current_thread(lock));
 
    sema_init(&waiter.semaphore, 0);
    if(!thread_mlfqs) {
    /* Assign a priority to the semaphore and insert it into the list
     * in order of priority. */
    waiter.semaphore.priority = thread_current()->priority;
    list_insert_ordered(&(cond->waiters), &(waiter.elem), 
                        sema_less_func, NULL);
    }
    lock_release(lock);
    sema_down(&waiter.semaphore);
    lock_acquire(lock);
}

/*! If any threads are waiting on COND (protected by LOCK), then
    this function signals one of them to wake up from its wait.
    LOCK must be held before calling this function.

    An interrupt handler cannot acquire a lock, so it does not
    make sense to try to signal a condition variable within an
    interrupt handler. */
void cond_signal(struct condition *cond, struct lock *lock UNUSED) {
    ASSERT(cond != NULL);
    ASSERT(lock != NULL);
    ASSERT(!intr_context ());
    ASSERT(lock_held_by_current_thread (lock));

    if (!list_empty(&cond->waiters)) 
        sema_up(&list_entry(list_pop_front(&cond->waiters),
                            struct semaphore_elem, elem)->semaphore);
}

/*! Wakes up all threads, if any, waiting on COND (protected by
    LOCK).  LOCK must be held before calling this function.

    An interrupt handler cannot acquire a lock, so it does not
    make sense to try to signal a condition variable within an
    interrupt handler. */
void cond_broadcast(struct condition *cond, struct lock *lock) {
    ASSERT(cond != NULL);
    ASSERT(lock != NULL);

    while (!list_empty(&cond->waiters))
        cond_signal(cond, lock);
}

/* Helper function passed as a parameter to list functions that
 * tell it how to compare two elements of a thread list. */
static bool thread_less_func(const struct list_elem *l, const struct list_elem *r, void *aux) {
  struct thread *lthread, *rthread;
  ASSERT (l != NULL && r != NULL);
  lthread = list_entry(l, struct thread, elem);
  rthread = list_entry(r, struct thread, elem);
  if(!thread_mlfqs) {
    return (lthread->priority >= rthread->priority);
  }
  else {
    return (lthread->mlfq_priority >= rthread->mlfq_priority); 
  }
}

/* Helper function passed as a parameter to list functions that
 * tell it how to compare two elements of a lock list. */
static bool lock_less_func(const struct list_elem *l, const struct list_elem *r, void *aux) {
  struct lock *llock, *rlock;
  ASSERT (l != NULL && r != NULL);
  llock = list_entry(l, struct lock, lockelem);
  rlock = list_entry(r, struct lock, lockelem);
  return (llock->priority > rlock->priority);
}

/* Helper function passed as a parameter to list functions that
 * tell it how to compare two elements of a semaphore_elem list. */
static bool sema_less_func(const struct list_elem *l, const struct list_elem *r, void *aux) {
  struct semaphore_elem *lsema, *rsema;
  ASSERT (l != NULL && r != NULL);
  lsema = list_entry(l, struct semaphore_elem, elem);
  rsema = list_entry(r, struct semaphore_elem, elem);
  return (lsema->semaphore.priority > rsema->semaphore.priority);
}

