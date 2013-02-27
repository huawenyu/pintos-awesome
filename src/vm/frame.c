#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/palloc.h"
#include "lib/kernel/list.h"
#include "userprog/pagedir.h"

#include "vm/frame.h"

// Ensure synchronization on VM operations
static struct lock vm_lock;

// Add and remove frames from the frames list
static bool vm_add_frame(void *);
static void vm_remove_frame(void *);

// Get the frame that corresponds to said page
static struct vm_frame *vm_get_frame(void *);

// Initialize all necessary structures
void vm_frame_init() {
  list_init(&vm_frames_list);
  lock_init(&vm_lock);
}

// Attempt to allocate a frame.
void *vm_frame_alloc(enum palloc_flags flags) {
  void *frame = NULL;

  // Should never be called on kernel space.
  if(!(flags & PAL_USER)) { return NULL; }

  frame = palloc_get_page(flags);

  // Success means we got a frame -- add it to the table.
  // Failure means none available -- need to evict
  if(frame != NULL) {
    vm_add_frame(frame);
  }
  else if( (frame = vm_evict_frame()) == NULL ) {
    PANIC("Could not allocate a user frame");
  }

  return frame;
}

// Frees the frame and removes it from the frame table
void vm_free_frame(void *frame) {
  vm_remove_frame(frame);
  palloc_free_page(frame);
}

// Evict a frame.
void *vm_evict_frame() {
  return NULL; // TODO
}

// Add the frame to the frame table
static bool vm_add_frame(void *frame) {
  struct vm_frame *v;
  v = malloc(sizeof(struct vm_frame));
  if (v == NULL) {
    return false; 
  }
  
  v->page = frame;
  v->tid = thread_current()->tid;

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
