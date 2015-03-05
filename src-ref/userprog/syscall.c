#include "devices/shutdown.h"
#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <user/syscall.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "userprog/pagedir.h"
#include "threads/synch.h"
#include "filesys/filesys.h"
#include "filesys/file.h"

static void syscall_handler(struct intr_frame *);
static int get_four_bytes_user(const void *);
static int get_user(const uint8_t *);
static bool put_user(uint8_t *, uint8_t);
static struct file_desc *get_file_descriptor(int);
static struct mapped_file *get_mapped_file(mapid_t);
void halt(void);
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

#ifdef VM
mapid_t mmap(int, void *);
void munmap(mapid_t);
#endif

void syscall_init(void) {
  intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");
}

void halt(void) {
  shutdown_power_off();
}

#ifdef VM
mapid_t mmap(int fd, void *addr) {
  struct thread *curr = thread_current();
  int i;
  
  if (addr == 0) {
    return -1;
  }
  
  // Check validity of fd
  if (fd == 0 || fd == 1) {
    return -1;
  }
  
  struct file_desc *d = get_file_descriptor(fd);
  
  if (d && d->file) {
    int flength = file_length(d->file);
    if (flength == 0) {
      return -1;
    }
    
    int num_pages = flength / PGSIZE + 1;
    
    if ((int) addr & (PGSIZE - 1) != 0) {
      return -1;
    }
    
    void *curr_addr = addr;
    for (i = 0; i < num_pages; i++) {
      if (pagedir_get_page(curr->pagedir, curr_addr) != NULL) {
        return -1;
      }
    }
    
    // We should be ready to actually map at this point.
    struct mapped_file *ms = palloc_get_page(0);
    ms->page = addr;
    ms->length = num_pages;
    if (list_empty(&(thread_current()->mapped_files))) {
      ms->id = 0;
    }
    else
    {
      ms->id = list_entry(list_back(&(thread_current()->mapped_files)), 
        struct mapped_file, elem)->id + 1;
    }
    
    curr_addr = addr;
    int offset = 0;
    // Alter the SPT, add entries for each page
    for (i = 0; i < num_pages; i++) {
      if (flength >= PGSIZE) {
        vm_install_mmap_spte(curr_addr, d->file, offset, PGSIZE, 0, true);
      }
      else {
        vm_install_mmap_spte(curr_addr, d->file, offset, flength, 
                             PGSIZE - flength, true);
      }
      curr_addr += PGSIZE;
      offset += PGSIZE;
    }
    
    list_push_back(&(thread_current()->mapped_files), &(ms->elem));
    return ms->id;
  }
  
  return -1;
}

void munmap(mapid_t mapping) {
  struct thread *curr = thread_current();
  int i;
  
  struct mapped_file *ms = get_mapped_file(mapping);
  if (ms) {
    struct vm_spte *page = vm_lookup_spte(ms->page);
    int num_pages = ms->length;
    
    struct file *f = file_reopen(page->file);
    
    for (i = 0; i < num_pages; i++) {
      void *addr = page->uaddr;
      if (pagedir_is_dirty(curr->pagedir, addr)) {
        file_write_at(f, addr, page->read_bytes, page->offset);
      }
      pagedir_clear_page(curr->pagedir, addr);
      vm_free_spte(page->uaddr);
      page = vm_lookup_spte(ms->page + PGSIZE);
    }
    
    file_close(f);
    
    list_remove(&(ms->elem));
    palloc_free_page(ms);
  }
}
#endif

void exit(int status) {
  printf("%s: exit(%d)\n", thread_current()->name, status);
  struct thread *curr = thread_current();
  struct file_desc *fd;
  struct mapped_file *ms;
  struct child_thread *ct;
  struct list_elem *l;
  struct list_elem *e;
  
#ifdef VM
  while (!list_empty(&(curr->mapped_files))) {
    l = list_begin(&(curr->mapped_files));
    ms = list_entry(l, struct mapped_file, elem);
    /* This should remove the ms from the ms list. */
    munmap(ms->id);
  }
#endif
  
  while (!list_empty(&(curr->file_descs))) {
    l = list_begin(&(curr->file_descs));
    fd = list_entry(l, struct file_desc, elem);
    /* This should remove the fd from the fd list. */
    close(fd->id);
  }
  
  if (curr->executable) {
    file_allow_write(curr->executable);
    file_close(curr->executable);
  }
  
  // Update children
  while (!list_empty(&(curr->child_threads))) {
    l = list_begin(&(curr->child_threads));
    ct = list_entry(l, struct child_thread, elem);
    /* Orphans! */
    if (!ct->exited) {
      struct thread *orphan = get_thread_from_tid(ct->pid);
      orphan->parent_pid = 1; //main pid
    }
    list_remove(&(ct->elem));
    palloc_free_page(ct);
  }
  
  // Update parent, unblock if necessary
  struct thread *parent = get_thread_from_tid(curr->parent_pid);
  for (e = list_begin(&(parent->child_threads));
       e != list_end(&(parent->child_threads));
       e = list_next(e)) {
    struct child_thread *ct = list_entry(e, struct child_thread, elem);
    if (ct->pid == curr->tid) {
      ct->exited = true;
      ct->exit_status = status;
      if (ct->waiting) thread_unblock(parent);
    }
  }

  // Release all locks
  for (e = list_begin(&(curr->locks));
       e != list_end(&(curr->locks));
       e = list_next(e)) {
    struct lock *curr_lock = list_entry(e, struct lock, lockelem);
    lock_release(curr_lock);
  }

  thread_exit();
}

pid_t exec(const char *cmd_line) {
  tid_t child_tid;
  
  if (cmd_line >= PHYS_BASE || get_user(cmd_line) == -1) {
      exit(-1);
      return -1;
  }
  
  child_tid = process_execute(cmd_line);

  return child_tid;
}

int wait(pid_t pid) {
  return process_wait(pid);
}

bool create(const char *file, unsigned initial_size) {
 	bool retval;
  
  // Check validity of file pointer
  if (file + initial_size - 1 >= PHYS_BASE || 
    get_user(file + initial_size - 1) == -1) {
      exit(-1);
      return -1;
  }
  
  retval = filesys_create(file, initial_size);
  return retval;
}

bool remove(const char *file) {
 	bool retval;
  
  // Check validity of file pointer
  if (file >= PHYS_BASE || get_user(file) == -1) {
      exit(-1);
      return -1;
  }
  
  retval = filesys_remove(file);
  return retval;
}

int open (const char *file) {
  struct file *f;
  struct file_desc *fd = palloc_get_page(0);
  
  // Check validity of file pointer
  if (file >= PHYS_BASE || get_user(file) == -1) {
      exit(-1);
      return -1;
  }
  
  f = filesys_open(file);
  if (!f) {
    return -1;
  }
  
  fd->file = f;
  if (list_empty(&(thread_current()->file_descs))) {
    fd->id = 3;
  }
  else {
    fd->id = list_entry(list_back(&(thread_current()->file_descs)), 
      struct file_desc, elem)->id + 1;
  }
  list_push_back(&(thread_current()->file_descs), &(fd->elem));
  
  return fd->id;
}

int filesize(int fd) {
  struct file_desc *d = get_file_descriptor(fd);
  int retval = -1;
  
  if (d && d->file) {
    retval = file_length(d->file);
  }
  
  return retval;
}

int read(int fd, void *buffer, unsigned size) {
  int retval = -1;
  int offset;
  
  // Check validity of buffer pointer
  if(buffer + size - 1 >= PHYS_BASE || get_user(buffer + size - 1) == -1) {
    exit(-1);
    return -1;
  }

  // Reading from console
  if(fd == 0) {
    for (offset = 0; offset != size; ++offset) {
      *(uint8_t *)(buffer + offset) = input_getc();
    }
    return size;
  }
  
  struct file_desc *d = get_file_descriptor(fd);
  if (d && d->file) {
    retval = file_read(d->file, buffer, size);
  }
  
  return retval;
}

int write(int fd, const void *buffer, unsigned size) {
  int retval = -1;
  
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
  
  struct file_desc *d = get_file_descriptor(fd);
  if (d && d->file) {
    retval = file_write(d->file, buffer, size);
  }
  
  return retval;
}

void seek(int fd, unsigned position) {
  struct file_desc *d = get_file_descriptor(fd);
  
  if (d && d->file) {
    file_seek(d->file, position);
  }
}

unsigned tell(int fd) {
  struct file_desc *d = get_file_descriptor(fd);
  int retval = -1;
  
  if (d && d->file) {
    retval = file_tell(d->file);
  }
  
  return retval;
}

void close (int fd) {
  struct file_desc *d = get_file_descriptor(fd);
  if (d && d->file) {
    file_close(d->file);
    
    list_remove(&(d->elem));
    palloc_free_page(d);
  }
}

static struct file_desc *get_file_descriptor(int fd) {
  struct list_elem *e;
  
  for (e = list_begin(&(thread_current()->file_descs)); 
       e != list_end(&(thread_current()->file_descs));
       e = list_next(e)) {
  
    struct file_desc *d = list_entry(e, struct file_desc, elem);
    if (d->id == fd) {
      return d;
    }
  }
  
  return NULL;
}

static struct mapped_file *get_mapped_file(mapid_t ms) {
  struct list_elem *e;
  
  for (e = list_begin(&(thread_current()->mapped_files)); 
       e != list_end(&(thread_current()->mapped_files));
       e = list_next(e)) {
  
    struct mapped_file *d = list_entry(e, struct mapped_file, elem);
    if (d->id == ms) {
      return d;
    }
  }
  
  return NULL;
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
#ifdef VM
      case SYS_MMAP:
        f->eax = (mapid_t) mmap(get_four_bytes_user(f->esp + 4),
                                (void *) get_four_bytes_user(f->esp + 8));
        break;
      case SYS_MUNMAP:
        munmap((mapid_t) get_four_bytes_user(f->esp + 4));
        break;
#endif
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
  if(add >= PHYS_BASE) { exit(-1); }
  uint8_t *uaddr = (uint8_t *) add;
  int result = 0;
  int temp;
  temp = get_user(uaddr);
  if(temp == -1) { exit(-1); }
  result += (temp << 0);
  temp = get_user(uaddr + 1);
  if(temp == -1) { exit(-1); }
  result += (temp << 8);
  temp = get_user(uaddr + 2);
  if(temp == -1) { exit(-1); }
  result += (temp << 16);
  temp = get_user(uaddr + 3);
  if(temp == -1) { exit(-1); }
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

