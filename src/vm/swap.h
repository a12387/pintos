#ifndef VM_SWAP_H
#define VM_SWAP_H
#include <stdint.h>
#include "devices/block.h"
#include <bitmap.h>

#define SECTORS_PER_PAGE (PGSIZE / BLOCK_SECTOR_SIZE)
struct swaptable {
  struct bitmap *used_map;
  struct block *swap_block;
};

void swap_init(void);
// physical page here
uint32_t swap_to_disk(void *page);
uint32_t swap_to_disk_at(void *page, int sector);
void swap_from_disk(void *page, int pos); 

#endif