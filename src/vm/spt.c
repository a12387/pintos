#include "vm/spt.h"
#include "threads/vaddr.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "threads/pte.h"
#include "vm/frame.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
void spt_free(struct spt *s) {
  list_remove(&s->elem);

  int nbytes = s->nbytes;

  int old_ofs = file_tell(s->file);
  for(int i = 0; i < nbytes; i += PGSIZE) {
    void *uaddr = (void *)s->start_uaddr + i;
    uint32_t *pte = lookup_page(thread_current()->pagedir, uaddr, false);
    if (pte != NULL) {
      if (*pte & PTE_P) {
        if (*pte & PTE_D) {
          int size_to_write = nbytes - i;
          if (size_to_write > PGSIZE)
            size_to_write = PGSIZE;
          file_seek(s->file, s->offset + i);
          file_write(s->file, uaddr, size_to_write);
        } 
        void *pa = pte_get_page(*pte);
        frametable_delete_all(pa);
        palloc_free_page(pa);
      } 
      *pte = 0;
    }
  }
  file_seek(s->file, old_ofs);
}