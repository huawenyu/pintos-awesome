#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "threads/thread.h"

struct vm_frame {
  void *page; // The kernel address for the frame
  tid_t tid;
  struct list_elem elem;

  // TODO: add more data that is needed here
};

struct list vm_frames_list;

void vm_frame_init(void);
void *vm_frame_alloc(enum palloc_flags flags);
void vm_free_frame(void *);

void *vm_evict_frame(void);

#endif // VM_FRAME_H
