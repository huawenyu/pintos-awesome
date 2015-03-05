#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/palloc.h"
#include "lib/kernel/list.h"
#include "userprog/pagedir.h"

#include "vm/page.h"

// Deletes an SPTE given its hash_elem
void delete_spte(struct hash_elem *, void *);

// Initialize the SPT
void vm_init_spt() {
  // Nothing to do, because SPT is thread-specific
}

// Shamelessly stolen from the hash table example in the docs
unsigned spte_hash(const struct hash_elem *p_, void *aux UNUSED) {
  const struct vm_spte *p = hash_entry(p_, struct vm_spte, elem);
  return hash_bytes(&p->uaddr, sizeof p->uaddr);
}

bool spte_less(const struct hash_elem *a_, const struct hash_elem *b_,
               void *aux UNUSED) {
  const struct vm_spte *a = hash_entry(a_, struct vm_spte, elem);
  const struct vm_spte *b = hash_entry(b_, struct vm_spte, elem);
  return a->uaddr < b->uaddr;
}

bool vm_install_fs_spte(void *uaddr, struct file *f, off_t offset,
                        uint32_t read_bytes, uint32_t zero_bytes,
                        bool writable) {
  struct thread *t = thread_current();

  struct vm_spte *spte;
  spte = malloc(sizeof(struct vm_spte));
  if(spte == NULL) { return false; }

  spte->uaddr = uaddr;
  spte->type = SPTE_FS;
  spte->access_vector = 0;
  spte->file = f;
  spte->offset = offset;
  spte->read_bytes = read_bytes;
  spte->zero_bytes = zero_bytes;
  spte->writable = writable;

  return hash_insert(&t->supp_pagedir, &(spte->elem)) == NULL;
}

bool vm_install_mmap_spte(void *uaddr, struct file *f, off_t offset,
                        uint32_t read_bytes, uint32_t zero_bytes,
                        bool writable) {
  struct thread *t = thread_current();

  struct vm_spte *spte;
  spte = malloc(sizeof(struct vm_spte));
  if(spte == NULL) { return false; }

  spte->uaddr = uaddr;
  spte->type = SPTE_MMAP;
  spte->access_vector = 0;
  spte->file = f;
  spte->offset = offset;
  spte->writable = writable;

  return hash_insert(&t->supp_pagedir, &(spte->elem)) == NULL;
}

bool vm_install_swap_spte(void *uaddr, int swap_page, bool writable) {
  struct thread *t = thread_current();  

  struct vm_spte *spte;
  spte = malloc(sizeof(struct vm_spte));
  if(spte == NULL) { return false; }
  
  spte->uaddr = uaddr;
  spte->type = SPTE_SWAP;
  spte->access_vector = 0;
  spte->swap_page = swap_page;
  spte->writable = writable;

  return hash_insert(&t->supp_pagedir, &(spte->elem)) == NULL;
}

bool vm_install_zero_spte(void *uaddr, bool writable) {
  struct thread *t = thread_current();

  struct vm_spte *spte;
  spte = malloc(sizeof(struct vm_spte));
  if(spte == NULL) { return false; }

  spte->uaddr = uaddr;
  spte->type = SPTE_ZERO;
  spte->access_vector = 0;
  spte->writable = writable;

  return hash_insert(&t->supp_pagedir, &(spte->elem)) == NULL;
}

bool vm_set_kaddr(const void *uaddr, void *kaddr) {
  struct vm_spte *v;
  v = vm_lookup_spte(uaddr);
  if(v == NULL) { return false; }

  v->kaddr = kaddr;
  return true; 
}

struct vm_spte *vm_lookup_spte(const void *uaddr) {
  struct vm_spte entry;
  struct hash_elem *e;
  struct thread *t = thread_current();

  entry.uaddr = uaddr;
  e = hash_find(&(t->supp_pagedir), &(entry.elem));
  return e != NULL ? hash_entry(e, struct vm_spte, elem) : NULL;
}

void vm_free_spte(void *uaddr) {
  struct thread *t = thread_current();
  struct vm_spte *e = vm_lookup_spte(uaddr);
  if(e == NULL) { PANIC("Shouldn't be deleting nonexistent spte"); }

  hash_delete(&(t->supp_pagedir), &(e->elem));
  free(e);
}

void delete_spte(struct hash_elem *elem, void *aux UNUSED) {
  struct vm_spte *e = hash_entry(elem, struct vm_spte, elem);
  free(e);
}

void vm_free_spt() {
  struct thread *t = thread_current();
  hash_destroy(&(t->supp_pagedir), *delete_spte); 
}
