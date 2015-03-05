#ifndef VM_PAGE_H
#define VM_PAGE_H

#include "filesys/file.h"
#include "threads/thread.h"
#include "lib/kernel/hash.h"

enum vm_spt_type {
  SPTE_FS = 001,  // On the file system
  SPTE_SWAP = 002, // In swap 
  SPTE_ZERO = 003, // All zeroes 
  SPTE_MMAP = 004 // Mmapped file
};

struct vm_spte {
  void *uaddr; // Only here to act as a key
  bool writable; // Just so much easier to keep track of here 
  enum vm_spt_type type;
  int access_vector; // Currently unused

  // Used iff of type SPTE_FS or SPTE_MMAP
  struct file *file;
  off_t offset;
  uint32_t read_bytes;
  uint32_t zero_bytes;
  
  // used iff of type SPTE_SWAP
  int swap_page;

  // Used iff page is currently present
  void *kaddr;

  struct hash_elem elem;

  // TODO: add more data as needed here
};

// Some stuff
void vm_init_spt(void);
bool vm_install_fs_spte(void *, struct file *, off_t, uint32_t, uint32_t, bool);
bool vm_install_mmap_spte(void *, struct file *, off_t, uint32_t, uint32_t, bool);
bool vm_install_swap_spte(void *, int, bool);
bool vm_install_zero_spte(void *, bool);
bool vm_set_kaddr(const void *, void *);
struct vm_spte *vm_lookup_spte(const void *);
void vm_free_spte(void *);
void vm_free_spt(void);

// Hash table supporting functions
unsigned spte_hash(const struct hash_elem *, void *);
bool spte_less (const struct hash_elem *, const struct hash_elem *, void *);

#endif // VM_PAGE_H
