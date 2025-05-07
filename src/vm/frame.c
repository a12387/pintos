#include "vm/frame.h"
#include <threads/malloc.h>
#include "threads/synch.h"
static struct hash frametable;
static struct lock ft_lock;
static unsigned frametable_hash(const struct hash_elem *e, void *aux UNUSED) {
  const struct frame *f = hash_entry(e, struct frame, elem);
  return hash_bytes(&f->ppage, sizeof (f->ppage));
}

static bool frametable_less(const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED) {
  const struct frame *a = hash_entry(a_, struct frame, elem);
  const struct frame *b = hash_entry(b_, struct frame, elem);
  return a->ppage < b->ppage;
}

void frametable_init() {
  hash_init(&frametable, frametable_hash, frametable_less, NULL);
  lock_init(&ft_lock);
}

void frametable_insert(uint32_t *ppage, uint32_t *vpage) {
  struct thread *t = thread_current();

  struct frame *f = malloc(sizeof(struct frame));
  f->owner = t;
  f->ppage = ppage;
  f->vpage = vpage;
  lock_acquire(&ft_lock);
  hash_insert(&frametable, &f->elem);
  lock_release(&ft_lock);
}

struct frame *frametable_find(uint32_t *ppage) {
  struct frame f;
  f.ppage = ppage;

  struct hash_elem *e = hash_find(&frametable, &f.elem);
  if (e == NULL) return NULL;
  else return hash_entry(e, struct frame, elem);
}

void frametable_delete_all(uint32_t *ppage) {
  struct frame f;
  f.ppage = ppage;

  struct hash_elem *e = hash_delete(&frametable, &f.elem);
  struct frame *fr = hash_entry(e, struct frame, elem);
  free(fr);
}
