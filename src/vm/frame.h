#ifndef VM_FRAME_H
#define VM_FRAME_H
#include <stdint.h>
#include <hash.h>
#include "threads/thread.h"
struct frame {
  struct hash_elem elem;
  uint32_t *ppage; /**< Physical page addr*/

  // no sharing, one frame - one page
  uint32_t *vpage; /**< Virtual page addr */
  struct thread *owner;

  /**
   *  If implement sharing, may use following data structure:
   *  
   *  struct mapping {
   *    struct list_elem elem;
   *    uint32_t *vpage;
   *    struct thread *owner;
   *  };
   *  
   *  and replace the single pair with a list of mappings
   */
};
void frametable_init(void);
void frametable_insert(uint32_t *ppage, uint32_t *vpage);
struct frame *frametable_find(uint32_t *ppage);
void frametable_delete_all(uint32_t *ppage);

#endif