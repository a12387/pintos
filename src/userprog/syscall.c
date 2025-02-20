#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "threads/vaddr.h"
#include "devices/shutdown.h"
#include "userprog/process.h"
#include "threads/malloc.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
static void syscall_handler (struct intr_frame *);
static void
get_user(const void *uaddr, void *dest, int nbyte)
{
  if(!is_user_vaddr(uaddr)) {
    goto bad;
  }
  char *pa = pagedir_get_page(thread_current()->pagedir, uaddr);
  if(pa == NULL) {
    goto bad;
  }

  while(nbyte--) {
    *((char *)dest++) = *pa++;
  }
  return;
bad:
  thread_exit();
}

static void
write_user(const void *uaddr, char *src, int nbyte)
{
  if(!is_user_vaddr(uaddr)) {
    goto bad;
  }
  char *pa = pagedir_get_page(thread_current()->pagedir, uaddr);
  if(pa == NULL) {
    goto bad;
  }

  while(nbyte--) {
    *pa++ = *src++;
  }
  return;
bad:
  thread_exit();
}


int sys_halt(struct intr_frame *) NO_RETURN;
int sys_exit(struct intr_frame *) NO_RETURN;
int sys_exec(struct intr_frame *);
int sys_wait(struct intr_frame *);
int sys_create(struct intr_frame *);
int sys_remove(struct intr_frame *);
int sys_open(struct intr_frame *);
int sys_filesize(struct intr_frame *);

static int (*syscalls[])(struct intr_frame *) = {
  [SYS_HALT]    sys_halt,
  [SYS_EXIT]    sys_exit,
  [SYS_EXEC]    sys_exec,
  [SYS_WAIT]    sys_wait,
  [SYS_CREATE]  sys_create,
  [SYS_REMOVE]  sys_remove,
  [SYS_OPEN]    sys_open,
  [SYS_FILESIZE]sys_filesize,
};

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f) 
{
  int syscall_no;
  get_user(f->esp, &syscall_no, 4);
  if(syscall_no >= 0 && syscall_no < (sizeof(syscalls) / sizeof(syscalls[0]))) {
    f->eax = syscalls[syscall_no](f);
  } else {
    printf("unknown syscall num: %d\n", syscall_no);
    f->eax = -1;
  }
  
}

int
sys_halt(struct intr_frame *f UNUSED)
{
  shutdown_power_off();
  NOT_REACHED();
}

int 
sys_exit(struct intr_frame *f)
{
  int status;
  get_user(f->esp + 4, &status, 4);
  exit(status);
  NOT_REACHED();
}

int
sys_exec(struct intr_frame *f)
{
  char *cmdline;
  get_user(f->esp + 4, &cmdline, sizeof cmdline);
  if(!is_user_vaddr(cmdline))
    return -1;
  cmdline = pagedir_get_page(thread_current()->pagedir, cmdline);
  int ret = process_execute(cmdline);
  return ret;
}

int 
sys_wait(struct intr_frame *f)
{
  tid_t tid;
  get_user(f->esp + 4, &tid, sizeof tid);
  return process_wait(tid);
}

int
sys_create(struct intr_frame *f)
{
  char *file_name;
  uint32_t init_size;
  get_user(f->esp + 4, &file_name, sizeof file_name);
  if(!is_user_vaddr(file_name))
    return -1;
  get_user(f->esp + 4 + sizeof file_name, &init_size, sizeof init_size);
  file_name = pagedir_get_page(thread_current()->pagedir, file_name);
  lock_acquire(&file_lock);
  int ret = filesys_create(file_name, (off_t)init_size);
  lock_release(&file_lock);
  return ret;
}

int
sys_remove(struct intr_frame *f)
{
  char *file_name;
  get_user(f->esp + 4, &file_name, sizeof file_name);
  if(!is_user_vaddr(file_name))
    return -1;
  file_name = pagedir_get_page(thread_current()->pagedir, file_name);
  lock_acquire(&file_lock);
  int ret = filesys_remove(file_name);
  lock_release(&file_lock);
  return ret;
}

int
sys_open(struct intr_frame *f)
{
  char *file_name;
  get_user(f->esp + 4, &file_name, sizeof file_name);
  if (!is_user_vaddr(file_name))
    return -1;
  file_name = pagedir_get_page(thread_current()->pagedir, file_name);
  lock_acquire(&file_lock);
  struct file *ret = filesys_open(file_name);
  lock_release(&file_lock);
  for(int i = 0; i < NOFILE; i++) {
    if(thread_current()->open_file[i] == NULL) {
      thread_current()->open_file[i] = ret;
      return i + 2; // 2 for stdin and stdout
    }
  }
  return -1;
}

int 
sys_filesize(struct intr_frame *f)
{
  int fd;
  get_user(f->esp + 4, &fd, sizeof fd);
  fd -= 2;
  struct file *file;
  if(fd < 0 || fd >= NOFILE || (file = thread_current()->open_file[fd]) == NULL) {
    return -1;
  }
  lock_acquire(&file_lock);
  int ret = file_length(file);
  lock_release(&file_lock);
  return ret;
}