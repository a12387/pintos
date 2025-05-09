#ifndef VM_SWAP_H
#define VM_SWAP_H
#include <stdint.h>
#include "devices/block.h"
#include <bitmap.h>
#include "threads/synch.h"

#define SECTORS_PER_PAGE (PGSIZE / BLOCK_SECTOR_SIZE)

/**
 *  For convenience, put these variables together.
 *  Can be replaced by static vars.
 */
struct swaptable {
  struct bitmap *used_map;
  struct block *swap_block;
  struct lock block_lock;
  struct lock bm_lock;
};

void swap_init(void);
// physical page here
uint32_t swap_to_disk(void *page);
uint32_t swap_to_disk_at(void *page, int sector);
void swap_from_disk(void *page, int pos); 
void swap_free(int pos);

#endif