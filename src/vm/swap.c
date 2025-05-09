#include "vm/swap.h"
#include "threads/vaddr.h"
#include <debug.h>

static struct swaptable sw;
void swap_init() {
  sw.swap_block = block_get_role(BLOCK_SWAP);
  // 1 page = 8 sectors
  sw.used_map =    
    bitmap_create(block_size(sw.swap_block) / SECTORS_PER_PAGE); 
}

uint32_t swap_to_disk(void *page) {
  size_t sector = bitmap_scan_and_flip(sw.used_map, 0, 1, false);
  if (sector == BITMAP_ERROR) {
    PANIC("swap_to_disk: run out of swap file");
  }
  return swap_to_disk_at(page, sector);
}

uint32_t swap_to_disk_at(void *page, int sector) {
  block_sector_t start = sector * SECTORS_PER_PAGE;
  for (int i = 0; i < SECTORS_PER_PAGE; i++) {
    block_write(sw.swap_block, start + i, page);
    page += BLOCK_SECTOR_SIZE;
  }
  return sector;
}

void swap_from_disk(void *page, int pos) {
  block_sector_t start = pos * SECTORS_PER_PAGE;

  for (int i = 0; i < SECTORS_PER_PAGE; i++) {
    block_read(sw.swap_block, start + i, page);
    page += BLOCK_SECTOR_SIZE;
  }

  // bitmap_set(sw.used_map, pos, false);
}