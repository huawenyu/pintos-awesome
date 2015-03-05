#ifndef VM_SWAP_H
#define VM_SWAP_H

void vm_swap_init(void);
void vm_swap_read(int, const void *);
int vm_swap_write(const void *);
void vm_swap_remove(int);


#endif
