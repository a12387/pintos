#include "vm/swap.h"
#include "threads/vaddr.h"
#include <debug.h>

static struct swaptable sw;   /**< Global swaptable */

/** 
 *  Init swaptable
 */
void swap_init() {
  sw.swap_block = block_get_role(BLOCK_SWAP);
  // 1 page = 8 sectors
  sw.used_map =    
    bitmap_create(block_size(sw.swap_block) / SECTORS_PER_PAGE); 
  lock_init(&sw.block_lock);
  lock_init(&sw.bm_lock);
}

/**
 *  Swap the given physical page to swap space.
 *  It will find the first available 8 sectors and
 *  put the page into disk.
 */
uint32_t swap_to_disk(void *page) {
  lock_acquire(&sw.bm_lock);
  size_t sector = bitmap_scan_and_flip(sw.used_map, 0, 1, false);
  lock_release(&sw.bm_lock);
  if (sector == BITMAP_ERROR) {
    PANIC("swap_to_disk: run out of swap file");
  }
  return swap_to_disk_at(page, sector);
}


/**
 *  Swap the given physical page to the given sectors of swap space
 */
uint32_t swap_to_disk_at(void *page, int sector) {
  block_sector_t start = sector * SECTORS_PER_PAGE;
  for (int i = 0; i < SECTORS_PER_PAGE; i++) {
    lock_acquire(&sw.block_lock);
    block_write(sw.swap_block, start + i, page);
    lock_release(&sw.block_lock);
    page += BLOCK_SECTOR_SIZE;
  }
  return sector;
}

/**
 *  Swap data from disk to page
 *  The data may be used later so cannot free the swap slot
 */
void swap_from_disk(void *page, int pos) {
  block_sector_t start = pos * SECTORS_PER_PAGE;

  for (int i = 0; i < SECTORS_PER_PAGE; i++) {
    lock_acquire(&sw.block_lock);
    block_read(sw.swap_block, start + i, page);
    lock_release(&sw.block_lock);
    page += BLOCK_SECTOR_SIZE;
  }
}

/**
 *  When a process stops, use this function to free its swap slots. 
 */
void swap_free(int pos) {
  lock_acquire(&sw.bm_lock);
  bitmap_set(sw.used_map, pos, false);
  lock_release(&sw.bm_lock);
}