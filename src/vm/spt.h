#ifndef VM_SPT_H
#define VM_SPT_H
#include <stdint.h>
#include <stdbool.h>
#include "filesys/file.h"
#include "list.h"
enum spt_type {
  SPT_FILE = 1,
  SPT_SWAP = 2,
};

struct spt {
  uint32_t *start_uaddr;
  uint32_t nbytes;
  struct file *file;
  uint32_t pos;
  uint32_t offset;
  enum spt_type type;
  bool writable;
  struct list_elem elem;
};

#endif