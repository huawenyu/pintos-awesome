#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#ifdef VM
#include "vm/page.h"
#endif

void syscall_init(void);
void exit(int);

typedef int pid_t;

#endif /* userprog/syscall.h */

