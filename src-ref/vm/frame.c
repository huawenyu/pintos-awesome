#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "threads/malloc.h"
#include "threads/synch.h"
#include "lib/kernel/list.h"
#include "userprog/pagedir.h"

#include "vm/frame.h"
#include "vm/page.h"
#include "vm/swap.h"
#include "filesys/file.h"
#include "threads/interrupt.h"

// Ensure synchronization on VM operations
static struct lock vm_lock;

// Add and remove frames from the frames list
static bool vm_add_frame(void *, void *);
static void vm_remove_frame(void *);

// Get the frame that corresponds to said page
static struct vm_frame *vm_get_frame(void *);

// Initialize all necessary structures
void vm_frame_init() {
  list_init(&vm_frames_list);
  lock_init(&vm_lock);
}

// Attempt to allocate a frame.
void *vm_frame_alloc(enum palloc_flags flags, void *uaddr) {
  void *frame = NULL;

  // Should never be called on kernel space.
  if(!(flags & PAL_USER)) { return NULL; }

  frame = palloc_get_page(flags);

  // Success means we got a frame -- add it to the table.
  // Failure means none available -- need to evict
  if(frame != NULL) {
    vm_add_frame(frame, uaddr);
  }
  else if( (frame = vm_evict_frame(uaddr)) == NULL ) {
    PANIC("Could not allocate a user frame");
  }
  
  return frame;
}

void vm_frame_set_done(void *frame, bool val) {
  struct vm_frame *v;
  v = vm_get_frame(frame);
  v->done = val;
}

// Frees the frame and removes it from the frame table
void vm_free_frame(void *frame) {
  vm_remove_frame(frame);
  palloc_free_page(frame);
}

// Free all frames owned by a tid.
void vm_free_tid_frames(tid_t tid) {
  struct vm_frame *v;
  struct list_elem *e;

  lock_acquire(&vm_lock);
  e = list_head(&vm_frames_list);
  while(e != list_tail(&vm_frames_list)) {
    v = list_entry(e, struct vm_frame, elem);
    if(v->tid == tid) {
      list_remove(e);
      free(v);
    }
    e = list_next(e);
  }
  lock_release(&vm_lock);
}

/* Update age for all frames. */
void vm_frame_tick(int64_t cur_ticks) {
  struct list_elem *e;
  struct vm_frame *v;
  struct thread *t;
  uint32_t *pd;
  const void *uaddr;
  bool accessed;

#ifdef USERPROG
  if (cur_ticks % FRAME_TIMER_FREQ == 0) {
  //if (cur_ticks % 1 == 0) {
    e = list_head(&vm_frames_list);
    while (e != list_tail(&vm_frames_list)) {
      v = list_entry(e, struct vm_frame, elem);
      t = get_thread_from_tid(v->tid);
      /* Quick fix... probably need to chance this later. */
      if (t != NULL) {
        pd = t->pagedir;
        uaddr = v->uaddr;
        accessed = pagedir_is_accessed(pd, uaddr);
        v->count = v->count >> 1;
        v->count &= (accessed << (sizeof(v->count) - 1));
        pagedir_set_accessed(pd, uaddr, false);
      }
      e = list_next(e);
    }
  }
#endif
}

/* Aging eviction policy. */
struct vm_frame *vm_pick_evict() {
  int lowest_count;
  struct list_elem *e;
  struct vm_frame *v;
  struct vm_frame *frame;

  e = list_head(&vm_frames_list);
  v = list_entry(e, struct vm_frame, elem);
  lowest_count = v->count;
  while (e != list_tail(&vm_frames_list)) {
    if (v->count < lowest_count && v->done) {
      lowest_count = v->count;
      frame = v;
    }
    e = list_next(e);
    v = list_entry(e, struct vm_frame, elem);
  }
  return frame;
}

// Evict a frame.
/* To whoever implements this:
     * If a page is FS type:
        - If dirty, change to swap type in vm_spte and swap out
        - If not dirty, just remove from memory -- will read later
     * If a page is SWAP type:
        - Always swap out
     * If a page is ZERO type:
        - If dirty, change to swap type in vm_spte and swap out
        - If not dirty, remove from memory -- will zero later
     * If a page is MMAP type:
        - If dirty, write back to file
        - If not dirty, just remove from memory

        */
void *vm_evict_frame(void *new_uaddr) {
  /* The list vm_frames_list should never be empty when this function
   * is called. */
  struct vm_frame *evicted;
  struct thread *evicted_thread;
  void *evicted_page;
  struct vm_spte *evicted_spte;
  bool dirty;

  intr_disable();
  ASSERT(!list_empty(&vm_frames_list));
  /* Choose a frame to evict. */
  lock_acquire(&vm_lock);
  evicted = vm_pick_evict();
  lock_release(&vm_lock);
  evicted_thread = get_thread_from_tid(evicted->tid);
  evicted_page = evicted->uaddr;
  /* Remove references to the frame. */
  pagedir_clear_page(evicted_thread->pagedir, evicted_page);
  /* Write the page to the file system or to swap, if necessary. */
  dirty = pagedir_is_dirty(evicted_thread->pagedir, evicted_page);
  evicted_spte = vm_lookup_spte(evicted_page);
  if (evicted_spte->type == SPTE_FS) {
    if (dirty) {
      evicted_spte->type = SPTE_SWAP;
      evicted_spte->swap_page = vm_swap_write(evicted_page);
    }
  }
  else if (evicted_spte->type == SPTE_SWAP) {
    evicted_spte->swap_page = vm_swap_write(evicted_page);
  }
  else if (evicted_spte->type == SPTE_ZERO) {
    if (dirty) {
      evicted_spte->type = SPTE_SWAP;
      evicted_spte->swap_page = vm_swap_write(evicted_page);
    }
  }
  else if (evicted_spte->type == SPTE_MMAP) {
    if (dirty) {
      lock_acquire(&filesys_lock);
      file_write_at(evicted_spte->file, evicted_page,
                    evicted_spte->read_bytes, evicted_spte->offset);
      lock_release(&filesys_lock);
    }
  }
  /* Update the evicted frame's data. */
  evicted->uaddr = new_uaddr;
  evicted->tid = thread_current()->tid;
  evicted->done = false;
  evicted->count = 0;
  intr_enable();

  return evicted->page;
}

// Add the frame to the frame table
static bool vm_add_frame(void *frame, void *uaddr) {
  struct vm_frame *v;
  v = malloc(sizeof(struct vm_frame));
  if (v == NULL) {
    return false; 
  }
  
  v->page = frame;
  v->tid = thread_current()->tid;
  v->uaddr = uaddr;
  v->done = false;
  v->count = 0;

  // TODO populate data into v

  lock_acquire(&vm_lock);
  list_push_back(&vm_frames_list, &v->elem);
  lock_release(&vm_lock);
 
  return true; 
}

// Remove the frame from the frame table
static void vm_remove_frame(void *frame) {
  struct vm_frame *v;
  struct list_elem *e;

  lock_acquire(&vm_lock);
  for(e = list_head(&vm_frames_list); e != list_tail(&vm_frames_list);
      e = list_next(e)) {
    v = list_entry(e, struct vm_frame, elem);
    if(v->page == frame) {
      list_remove(e);
      free(v);
      break;
    } 
  }

  lock_release(&vm_lock);
}

// Retrieve a frame table entry that points to given page
static struct vm_frame *vm_get_frame(void *page) {
  struct vm_frame *v;
  struct list_elem *e;

  lock_acquire(&vm_lock);
  for(e = list_head(&vm_frames_list); e != list_tail(&vm_frames_list);
      e = list_next(e)) {
    v = list_entry(e, struct vm_frame, elem);
    if(v->page == page) {
      break;
    }
    v = NULL;
  }
  lock_release(&vm_lock);

  return v;
}
