#include "devices/shutdown.h"
#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "threads/synch.h"
#include "filesys/filesys.h"
#include "filesys/file.h"

static void syscall_handler(struct intr_frame *);
static int get_four_bytes_user(const void *);
static int get_user(const uint8_t *);
static bool put_user(uint8_t *, uint8_t);
void halt(void);
void exit(int);
pid_t exec(const char*);
int wait(pid_t);
bool create(const char*, unsigned);
bool remove(const char*);
int open(const char*);
int filesize(int);
int read(int, void*, unsigned);
int write(int, const void *, unsigned);
void seek(int, unsigned);
unsigned tell(int);
void close(int);

static struct lock *filesys_lock;

void syscall_init(void) {
    intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");
}

void halt(void) {
  shutdown_power_off();
}

// TODO: need to save status somewhere where parent thread can access
void exit(int status) {
  // TODO: Signal status to kernel
  printf("%s: exit(%d)\n", thread_current()->name, status);
  thread_exit();
}

// TODO: call process execute, get pid, set as child of current thread
pid_t exec(const char *cmd_line UNUSED) {
  // TODO
  thread_exit();
}

// TODO: implementation deferred
int wait(pid_t pid) {
  return process_wait(pid);
}

bool create(const char *file UNUSED, unsigned initial_size UNUSED) {
 	bool retval;
  
  // Check validity of file pointer
  if (file + initial_size - 1 >= PHYS_BASE || 
    get_user(file + initial_size - 1) == -1) {
      exit(-1);
      return -1;
    }
  
  lock_acquire(filesys_lock);
  retval = filesys_create(file, initial_size);
  lock_release(filesys_lock);
  return retval;
}

bool remove(const char *file UNUSED) {
 	bool retval;
  
  // Check validity of file pointer
  if (file >= PHYS_BASE || get_user(file) == -1) {
      exit(-1);
      return -1;
    }
  
  lock_acquire(filesys_lock);
  retval = filesys_remove(file);
  lock_release(filesys_lock);
  return retval;
}

int open (const char *file UNUSED) {

  // TODO
  thread_exit();
}

int filesize(int fd UNUSED) {

  // TODO
  thread_exit();
}

int read(int fd UNUSED, void *buffer UNUSED, unsigned size UNUSED) {

  // TODO
  thread_exit();
}

int write(int fd, const void *buffer, unsigned size) {
  // Check validity of buffer pointer
  if(buffer + size - 1 >= PHYS_BASE || get_user(buffer + size - 1) == -1) {
    exit(-1);
    return -1;
  }

  // Writing to the console
  if(fd == 1) {
    size_t offset = 0;
    while(offset + 200 < size) {
      putbuf((char *)(buffer + offset), (size_t) 200);
      offset = offset + 200;
    }
    putbuf( (char *)(buffer + offset), (size_t) (size - offset));
    return size;
  }

  return 0;
}

void seek(int fd UNUSED, unsigned position UNUSED) {

  // TODO
  thread_exit();
}

unsigned tell(int fd UNUSED) {

  // TODO
  thread_exit();
}

void close (int fd UNUSED) {

  // TODO
  thread_exit();
}

static void syscall_handler(struct intr_frame *f) {
  //  printf("system call!\n");
    int call_num = get_four_bytes_user(f->esp);
  //  printf("Call number: %d\n", call_num);

    switch(call_num) {
      case SYS_HALT:
        halt();
        NOT_REACHED();
      case SYS_EXIT:
        exit(get_four_bytes_user(f->esp + 4));
        NOT_REACHED();
      case SYS_EXEC:
        f->eax = (uint32_t) exec((const char *) get_four_bytes_user(f->esp + 4));
        break;
      case SYS_WAIT:
        f->eax = (uint32_t) wait((pid_t) get_four_bytes_user(f->esp + 4));
        break;
      case SYS_CREATE:
        f->eax = (uint32_t) create((const char *) get_four_bytes_user(f->esp + 4),
                                   (unsigned) get_four_bytes_user(f->esp + 8));
        break;
      case SYS_REMOVE:
        f->eax = (uint32_t) remove((const char *) get_four_bytes_user(f->esp + 4));
        break;
      case SYS_OPEN:
        f->eax = (uint32_t) open((const char *) get_four_bytes_user(f->esp + 4));
        break;
      case SYS_FILESIZE:
        f->eax = (uint32_t) filesize(get_four_bytes_user(f->esp + 4));
        break;
      case SYS_READ:
        f->eax = (uint32_t) read(get_four_bytes_user(f->esp + 4),
                                 (void *) get_four_bytes_user(f->esp + 8),
                                 (unsigned) get_four_bytes_user(f->esp + 12));
        break;
      case SYS_WRITE:
        f->eax = (uint32_t) write(get_four_bytes_user(f->esp + 4),
                                  (const void *) get_four_bytes_user(f->esp + 8),
                                  (unsigned) get_four_bytes_user(f->esp + 12));
        break;
      case SYS_SEEK:
        seek(get_four_bytes_user(f->esp + 4),
             (unsigned) get_four_bytes_user(f->esp + 8));
        break;
      case SYS_TELL:
        f->eax = (uint32_t) tell(get_four_bytes_user(f->esp + 4));
        break;
      case SYS_CLOSE:
        close(get_four_bytes_user(f->esp + 4));
        break;
      default:
        printf("Unimplemented system call number");
        thread_exit();
        break;

    }
}

// Reads a 4 byte value at virtual address ADD
// Terminates the thread with exit status 11 if there is a segfault
// (including trying to read above PHYS_BASE. That's not yo memory!)
static int get_four_bytes_user(const void * add) {
  if(add > PHYS_BASE) { exit(11); }
  uint8_t *uaddr = (uint8_t *) add;
  int result = 0;
  int temp;
  temp = get_user(uaddr);
  if(temp == -1) { exit(11); }
  result += (temp << 0);
  temp = get_user(uaddr + 1);
  if(temp == -1) { exit(11); }
  result += (temp << 8);
  temp = get_user(uaddr + 2);
  if(temp == -1) { exit(11); }
  result += (temp << 16);
  temp = get_user(uaddr + 3);
  if(temp == -1) { exit(11); }
  result += (temp << 24);
  return result;
}

/*! Reads a byte at user virtual address UADDR.
      UADDR must be below PHYS_BASE.
          Returns the byte value if successful, -1 if a segfault occurred. */
static int get_user(const uint8_t *uaddr) {
      int result;
          asm ("movl $1f, %0; movzbl %1, %0; 1:"
                       : "=&a" (result) : "m" (*uaddr));
              return result;
}

/*! Writes BYTE to user address UDST.
      UDST must be below PHYS_BASE.
          Returns true if successful, false if a segfault occurred. */
static bool put_user (uint8_t *udst, uint8_t byte) {
      int error_code;
          asm ("movl $1f, %0; movb %b2, %1; 1:"
                       : "=&a" (error_code), "=m" (*udst) : "q" (byte));
              return error_code != -1;
}

