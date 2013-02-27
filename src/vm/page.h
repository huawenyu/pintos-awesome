#ifndef VM_PAGE_H
#define VM_PAGE_H

#include "threads/thread.h"
#include "lib/kernel/hash.h"

enum vm_spt_type {
  SPTE_FS = 001,  // On the file system
  SPTE_SWAP = 002, // In swap 
  SPTE_ZERO = 003, // All zeroes 
  SPTE_MEM = 004 // Actively in memory
};

struct vm_spte {
  enum vm_spt_type type;
  bool writable;

  struct hash_elem elem;

  // TODO: add more data as needed here
};

// Some stuff
void vm_init_spt(void);

#endif // VM_PAGE_H
