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
#include "devices/input.h"
#include "lib/string.h"
static void syscall_handler (struct intr_frame *);
static bool
is_valid_string(char *s)
{
  while(is_user_vaddr(s) && pagedir_get_page(thread_current()->pagedir, s) != NULL) {
    while(1) {
      if(*s == '\0')
        return true;
      s++;
      if(((int)s & BITMASK(0, 12)) == 0) 
        break;
    }
  }
  return false;
}
static void
check_addr(void *uaddr) {
  if (!is_user_vaddr(uaddr)) {
    thread_exit();
  }
}
static void
get_user(void *uaddr, void *dest, int nbyte)
{
  check_addr(uaddr);
  check_addr(uaddr + nbyte);

  while(nbyte--) {
    *((char *)dest++) = *(char *)uaddr++;
  }
  return;
}

static void
write_user(void *uaddr, char *src, int nbyte)
{
  check_addr(uaddr);
  check_addr(uaddr + nbyte);
  while(nbyte--) {
    *(char *)uaddr++ = *src++;
  }
  return;
}


int sys_halt(struct intr_frame *) NO_RETURN;
int sys_exit(struct intr_frame *) NO_RETURN;
int sys_exec(struct intr_frame *);
int sys_wait(struct intr_frame *);
int sys_create(struct intr_frame *);
int sys_remove(struct intr_frame *);
int sys_open(struct intr_frame *);
int sys_filesize(struct intr_frame *);
int sys_read(struct intr_frame *);
int sys_write(struct intr_frame *);
int sys_seek(struct intr_frame *);
int sys_tell(struct intr_frame *);
int sys_close(struct intr_frame *);

static int (*syscalls[])(struct intr_frame *) = {
  [SYS_HALT]    sys_halt,
  [SYS_EXIT]    sys_exit,
  [SYS_EXEC]    sys_exec,
  [SYS_WAIT]    sys_wait,
  [SYS_CREATE]  sys_create,
  [SYS_REMOVE]  sys_remove,
  [SYS_OPEN]    sys_open,
  [SYS_FILESIZE]sys_filesize,
  [SYS_READ]    sys_read,
  [SYS_WRITE]   sys_write,
  [SYS_SEEK]    sys_seek,
  [SYS_TELL]    sys_tell,
  [SYS_CLOSE]   sys_close,
};

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f) 
{
  uint32_t syscall_no;
  get_user(f->esp, &syscall_no, 4);
  if(syscall_no < (sizeof(syscalls) / sizeof(syscalls[0]))) {
    f->eax = syscalls[syscall_no](f);
  } else {
    thread_exit();
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
  thread_current()->info->exit_status = status;
  thread_exit();
  NOT_REACHED();
}

int
sys_exec(struct intr_frame *f)
{
  char *cmdline;
  get_user(f->esp + 4, &cmdline, sizeof cmdline);
  if(!is_valid_string(cmdline))
    thread_exit();
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
  if(!is_valid_string(file_name))
    thread_exit();
  get_user(f->esp + 4 + sizeof file_name, &init_size, sizeof init_size);
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
  if(!is_valid_string(file_name))
    thread_exit();
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

  if (!is_valid_string(file_name))
    thread_exit();
  lock_acquire(&file_lock);
  struct file *ret = filesys_open(file_name);
  lock_release(&file_lock);
  if(ret == NULL)
    return -1;
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

int 
sys_read(struct intr_frame *f)
{
  int fd;
  char *buffer;
  uint32_t size;
  struct file *file;
  get_user(f->esp + 4, &fd, sizeof fd);
  fd -= 2;
  if((fd < 0 && fd + 2 != STDIN_FILENO) || fd >= NOFILE || (file = thread_current()->open_file[fd]) == NULL) {
    return -1;
  }
  get_user(f->esp + 4 + sizeof fd, &buffer, sizeof buffer);
  if(!is_user_vaddr(buffer) || buffer == NULL) {
    thread_exit();
  }
  get_user(f->esp + 4 + sizeof fd + sizeof buffer, &size, sizeof size);
  if(!is_user_vaddr(buffer + size)) {
    return -1;
  }

  uint32_t bytes_read = 0;
  if(fd + 2 == STDIN_FILENO) {
    while (bytes_read < size) {
      char c = input_getc();
      if(c == 13) {
        // Enter
        break;
      } else if (c == 8) {
        // Backspace
        bytes_read--;
      } else {
        write_user(buffer + bytes_read, &c, 1);
        bytes_read++;
      }
    }
    return bytes_read;
  } else {
    char *kbuffer = malloc(size);
    lock_acquire(&file_lock);
    bytes_read = file_read(file, kbuffer, size);
    lock_release(&file_lock);
    memcpy(buffer, kbuffer, bytes_read);
    free(kbuffer);
    return bytes_read;
  }
}

int 
sys_write(struct intr_frame *f) 
{
  int fd;
  char *buffer;
  uint32_t size;
  struct file *file;
  get_user(f->esp + 4, &fd, sizeof fd);
  fd -= 2;
  if((fd < 0 && fd + 2 != STDOUT_FILENO) || fd >= NOFILE || (file = thread_current()->open_file[fd]) == NULL) {
    return -1;
  }
  get_user(f->esp + 4 + sizeof fd, &buffer, sizeof buffer);
  if(!is_user_vaddr(buffer) || buffer == NULL) {
    thread_exit();
  }
  get_user(f->esp + 4 + sizeof fd + sizeof buffer, &size, sizeof size);
  if(!is_user_vaddr(buffer + size)) {
    return -1;
  }

  int byte_written = 0;
  if(fd + 2 == STDOUT_FILENO) {
    putbuf(buffer, size);
    return size;
  } else {
    char *kbuffer = malloc (size);
    memcpy(kbuffer, buffer, size);
    lock_acquire(&file_lock);
    byte_written = file_write(file, kbuffer, size);
    lock_release(&file_lock);
    free(kbuffer);
    return byte_written;
  }
}

int 
sys_seek(struct intr_frame *f)
{
  int fd;
  uint32_t pos;
  struct file *file;
  get_user(f->esp + 4, &fd, sizeof fd);
  fd -= 2;
  if(fd < 0 || fd >= NOFILE || (file = thread_current()->open_file[fd]) == NULL) {
    return -1;
  }

  get_user(f->esp + 4 + sizeof fd, &pos, sizeof pos);
  lock_acquire(&file_lock);
  file_seek(file, (off_t)pos);
  lock_release(&file_lock);
  return 0;
}

int 
sys_tell(struct intr_frame *f)
{
  int fd;
  struct file *file;
  get_user(f->esp + 4, &fd, sizeof fd);
  fd -= 2;
  if(fd < 0 || fd >= NOFILE || (file = thread_current()->open_file[fd]) == NULL) {
    return -1;
  }

  lock_acquire(&file_lock);
  int ret = file_tell(file);
  lock_release(&file_lock);
  return ret;
}

int 
sys_close(struct intr_frame *f)
{
  int fd;
  struct file *file;
  get_user(f->esp + 4, &fd, sizeof fd);
  fd -= 2;
  if(fd < 0 || fd >= NOFILE || (file = thread_current()->open_file[fd]) == NULL) {
    return -1;
  }

  lock_acquire(&file_lock);
  file_close(file);
  lock_release(&file_lock);
  thread_current()->open_file[fd] = NULL;
  return 0;
}