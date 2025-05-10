#ifndef VM_SPT_H
#define VM_SPT_H
#include <stdint.h>
#include <stdbool.h>
#include "filesys/file.h"
#include "list.h"

enum spt_type {
  SPT_FILE = 1,
  SPT_SWAP = 2,
  SPT_MMAP = 3
};

/** 
 *  The data structure for spt entry. 
 */
struct spt {
  uint32_t *start_uaddr;  /**< User addr from which the virtual memory area starts */
  uint32_t nbytes;        /**< Length of the vma */
  struct file *file;      /**< File backup, if vma loaded from file */
  uint32_t offset;        /**< File offset of the first byte in the vma */
  uint32_t pos;           /**< Swap sector [pos * 8, pos * 8 + 7] */
  enum spt_type type;     /**< Backup in file or swap space */
  bool writable;         
  struct list_elem elem; 
};

void spt_free(struct spt *s);

#endif