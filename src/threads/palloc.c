#include "threads/palloc.h"
#include <bitmap.h>
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "threads/loader.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "threads/pte.h"
#include "vm/swap.h"
#include "vm/frame.h"
#include <threads/malloc.h>
/** Page allocator.  Hands out memory in page-size (or
   page-multiple) chunks.  See malloc.h for an allocator that
   hands out smaller chunks.

   System memory is divided into two "pools" called the kernel
   and user pools.  The user pool is for user (virtual) memory
   pages, the kernel pool for everything else.  The idea here is
   that the kernel needs to have memory for its own operations
   even if user processes are swapping like mad.

   By default, half of system RAM is given to the kernel pool and
   half to the user pool.  That should be huge overkill for the
   kernel pool, but that's just fine for demonstration purposes. */

/** A memory pool. */
struct pool
  {
    struct lock lock;                   /**< Mutual exclusion. */
    struct bitmap *used_map;            /**< Bitmap of free pages. */
    uint8_t *base;                      /**< Base of pool. */
  };

/** Two pools: one for kernel data, one for user pages. */
static struct pool kernel_pool, user_pool;
static int clock;
static struct lock clock_lock;
static void init_pool (struct pool *, void *base, size_t page_cnt,
                       const char *name);
static bool page_from_pool (const struct pool *, void *page);

/** Initializes the page allocator.  At most USER_PAGE_LIMIT
   pages are put into the user pool. */
void
palloc_init (size_t user_page_limit)
{
  /* Free memory starts at 1 MB and runs to the end of RAM. */
  uint8_t *free_start = ptov (1024 * 1024);
  uint8_t *free_end = ptov (init_ram_pages * PGSIZE);
  size_t free_pages = (free_end - free_start) / PGSIZE;
  size_t user_pages = free_pages / 2;
  size_t kernel_pages;
  if (user_pages > user_page_limit)
    user_pages = user_page_limit;
  kernel_pages = free_pages - user_pages;

  /* Give half of memory to kernel, half to user. */
  init_pool (&kernel_pool, free_start, kernel_pages, "kernel pool");
  init_pool (&user_pool, free_start + kernel_pages * PGSIZE,
             user_pages, "user pool");
             
  clock = bitmap_size(user_pool.used_map);
  lock_init(&clock_lock);
}

/** Obtains and returns a group of PAGE_CNT contiguous free pages.
   If PAL_USER is set, the pages are obtained from the user pool,
   otherwise from the kernel pool.  If PAL_ZERO is set in FLAGS,
   then the pages are filled with zeros.  If too few pages are
   available, returns a null pointer, unless PAL_ASSERT is set in
   FLAGS, in which case the kernel panics. */
void *
palloc_get_multiple (enum palloc_flags flags, size_t page_cnt)
{
  struct pool *pool = flags & PAL_USER ? &user_pool : &kernel_pool;
  void *pages;
  size_t page_idx;

  if (page_cnt == 0)
    return NULL;

  lock_acquire (&pool->lock);
  page_idx = bitmap_scan_and_flip (pool->used_map, 0, page_cnt, false);
  lock_release (&pool->lock);

  if (page_idx != BITMAP_ERROR)
    pages = pool->base + PGSIZE * page_idx;
  else
    pages = NULL;

  if (pages != NULL) 
    {
      if (flags & PAL_ZERO)
        memset (pages, 0, PGSIZE * page_cnt);
    }
  else 
    {
      if (flags & PAL_ASSERT)
        PANIC ("palloc_get: out of pages");
    }
  return pages;
}

/** Obtains a single free page and returns its kernel virtual
   address.
   If PAL_USER is set, the page is obtained from the user pool,
   otherwise from the kernel pool.  If PAL_ZERO is set in FLAGS,
   then the page is filled with zeros.  If no pages are
   available, returns a null pointer, unless PAL_ASSERT is set in
   FLAGS, in which case the kernel panics. */
void *
palloc_get_page (enum palloc_flags flags) 
{
  return palloc_get_multiple (flags, 1);
}

/**
 *  Obtain a physical page only for page fault
 *  since clock hand only moves when page fault occurs
 *  
 *  If a palloc is not in page fault and runs out of frames, 
 *  the kernel will stop since PAL_USER is not used anywhere 
 *  and this palloc is a kernel pool palloc.
 * 
 *  But if in page fault, undoubtedly it is to alloc a user frame
 *  and we can swap. 
 */
void *
palloc_get_page_for_page_fault ()
{

  uint32_t *ppage, *pte;
  int bmsize = bitmap_size(user_pool.used_map);

  // make sure `clock` is accessed by only one thread at a time
  while (1) {
    lock_acquire (&clock_lock);
    int local_clock = clock;
    clock++;
    if (clock % bmsize == 0) {
      clock = bmsize;
    }
    lock_release(&clock_lock);
    ppage = (uint32_t *)(user_pool.base + PGSIZE * (local_clock - bmsize) );
    

    if (!bitmap_test(user_pool.used_map, (local_clock) % bmsize)) {
      bitmap_flip(user_pool.used_map, (local_clock) % bmsize);
      
      return ppage;
    }
    struct frame *f = frametable_find(ppage);
    if (f == NULL) 
      continue;
    pte = lookup_page(f->owner->pagedir, f->vpage, false);
    
    if (*pte & PTE_A) {
      *pte &= ~PTE_A;
    } else {
      // prevent future accessing
      *pte &= ~(PTE_P);
      // evict 
      // 1. swap
      struct spt *s = NULL;
      bool found = false;
      int ptr_ofs;
      // try to find in spt list
      for (struct list_elem *e = list_begin(&f->owner->spt);
        e != list_end(&f->owner->spt); e = list_next(e)) {
        s = list_entry(e, struct spt, elem);
        ptr_ofs = (int)f->vpage - (int)s->start_uaddr;
        if(ptr_ofs >= 0 && (uint32_t)ptr_ofs < s->nbytes) {
          found = true;
          break;
        }
      }
      if (found && (*pte & PTE_D)) {
        // has file backup
        if (s->type == SPT_FILE) {
          int size_to_write = s->nbytes - ptr_ofs;
          if (size_to_write >= PGSIZE) {
            file_seek(s->file, s->offset + ptr_ofs);
            file_write(s->file, ppage, PGSIZE);
          } else {
            // only part of page has file backup
            // pretend as not found
            // and give the page to swap
            found = false;
            s->nbytes -= size_to_write;
          }
        }
        // has swap backup
        if (s->type == SPT_SWAP) {
          swap_to_disk_at(ppage, s->pos);
        }
      }
      // no backup (or just evicted from file to swap)
      if (!found && (*pte & PTE_D)) {
        // make new spt entry for swap
        found = true;
        s = malloc(sizeof (struct spt));
        s->file = NULL;
        s->nbytes = PGSIZE;
        s->offset = 0;
        s->pos = swap_to_disk(ppage);
        s->start_uaddr = f->vpage;
        s->type = SPT_SWAP;
        s->writable = (*pte & PTE_W) != 0;
        list_push_back(&f->owner->spt, &s->elem);
      }

      // 2. unmapping (make pte point to spt | NULL)
      if (found) {
        *pte = (uint32_t)s | PTE_L;
      } else {
        if (*pte & PTE_W)
          *pte = PTE_L | PTE_ZW; // zeros
        else 
          *pte = PTE_L;
      }
    
      frametable_delete_all(ppage);
      return ppage;
      // 3. return 
      // 4. establish new mapping in pagedir_set_page()
    }
  }
}

/** Frees the PAGE_CNT pages starting at PAGES. */
void
palloc_free_multiple (void *pages, size_t page_cnt) 
{
  struct pool *pool;
  size_t page_idx;

  ASSERT (pg_ofs (pages) == 0);
  if (pages == NULL || page_cnt == 0)
    return;

  if (page_from_pool (&kernel_pool, pages))
    pool = &kernel_pool;
  else if (page_from_pool (&user_pool, pages))
    pool = &user_pool;
  else
    NOT_REACHED ();

  page_idx = pg_no (pages) - pg_no (pool->base);

#ifndef NDEBUG
  memset (pages, 0xcc, PGSIZE * page_cnt);
#endif

  ASSERT (bitmap_all (pool->used_map, page_idx, page_cnt));
  bitmap_set_multiple (pool->used_map, page_idx, page_cnt, false);
}

/** Frees the page at PAGE. */
void
palloc_free_page (void *page) 
{
  palloc_free_multiple (page, 1);
}

/** Initializes pool P as starting at START and ending at END,
   naming it NAME for debugging purposes. */
static void
init_pool (struct pool *p, void *base, size_t page_cnt, const char *name) 
{
  /* We'll put the pool's used_map at its base.
     Calculate the space needed for the bitmap
     and subtract it from the pool's size. */
  size_t bm_pages = DIV_ROUND_UP (bitmap_buf_size (page_cnt), PGSIZE);
  if (bm_pages > page_cnt)
    PANIC ("Not enough memory in %s for bitmap.", name);
  page_cnt -= bm_pages;

  printf ("%zu pages available in %s.\n", page_cnt, name);

  /* Initialize the pool. */
  lock_init (&p->lock);
  p->used_map = bitmap_create_in_buf (page_cnt, base, bm_pages * PGSIZE);
  p->base = base + bm_pages * PGSIZE;
}

/** Returns true if PAGE was allocated from POOL,
   false otherwise. */
static bool
page_from_pool (const struct pool *pool, void *page) 
{
  size_t page_no = pg_no (page);
  size_t start_page = pg_no (pool->base);
  size_t end_page = start_page + bitmap_size (pool->used_map);

  return page_no >= start_page && page_no < end_page;
}
