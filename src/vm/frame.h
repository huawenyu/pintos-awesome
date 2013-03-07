#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "threads/thread.h"
#include "threads/palloc.h"

#define FRAME_TIMER_FREQ 100

struct vm_frame {
  void *page; // The kernel address for the frame
  tid_t tid;
  struct list_elem elem;
  void *uaddr;
  bool done; // Data is done being loaded into the page
  int count; // Number of times the frame has been accessed

  // TODO: add more data that is needed here
};

struct list vm_frames_list;

void vm_frame_init(void);
void *vm_frame_alloc(enum palloc_flags flags, void *);
void vm_frame_set_done(void *, bool);
void vm_free_frame(void *);
void vm_free_tid_frames(tid_t);

void vm_frame_tick(int64_t);
struct vm_frame* vm_pick_evict(void);
void *vm_evict_frame(void *);

#endif // VM_FRAME_H
