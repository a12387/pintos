#include "userprog/exception.h"
#include <inttypes.h>
#include <stdio.h>
#include "userprog/gdt.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "threads/pte.h"
#include "threads/palloc.h"
#include <string.h>
#include "filesys/file.h"
#include "vm/spt.h"
#include "vm/swap.h"
#include <threads/malloc.h>
/** Number of page faults processed. */
static long long page_fault_cnt;

static void kill (struct intr_frame *);
static void page_fault (struct intr_frame *);

/** Registers handlers for interrupts that can be caused by user
   programs.

   In a real Unix-like OS, most of these interrupts would be
   passed along to the user process in the form of signals, as
   described in [SV-386] 3-24 and 3-25, but we don't implement
   signals.  Instead, we'll make them simply kill the user
   process.

   Page faults are an exception.  Here they are treated the same
   way as other exceptions, but this will need to change to
   implement virtual memory.

   Refer to [IA32-v3a] section 5.15 "Exception and Interrupt
   Reference" for a description of each of these exceptions. */
void
exception_init (void) 
{
  /* These exceptions can be raised explicitly by a user program,
     e.g. via the INT, INT3, INTO, and BOUND instructions.  Thus,
     we set DPL==3, meaning that user programs are allowed to
     invoke them via these instructions. */
  intr_register_int (3, 3, INTR_ON, kill, "#BP Breakpoint Exception");
  intr_register_int (4, 3, INTR_ON, kill, "#OF Overflow Exception");
  intr_register_int (5, 3, INTR_ON, kill,
                     "#BR BOUND Range Exceeded Exception");

  /* These exceptions have DPL==0, preventing user processes from
     invoking them via the INT instruction.  They can still be
     caused indirectly, e.g. #DE can be caused by dividing by
     0.  */
  intr_register_int (0, 0, INTR_ON, kill, "#DE Divide Error");
  intr_register_int (1, 0, INTR_ON, kill, "#DB Debug Exception");
  intr_register_int (6, 0, INTR_ON, kill, "#UD Invalid Opcode Exception");
  intr_register_int (7, 0, INTR_ON, kill,
                     "#NM Device Not Available Exception");
  intr_register_int (11, 0, INTR_ON, kill, "#NP Segment Not Present");
  intr_register_int (12, 0, INTR_ON, kill, "#SS Stack Fault Exception");
  intr_register_int (13, 0, INTR_ON, kill, "#GP General Protection Exception");
  intr_register_int (16, 0, INTR_ON, kill, "#MF x87 FPU Floating-Point Error");
  intr_register_int (19, 0, INTR_ON, kill,
                     "#XF SIMD Floating-Point Exception");

  /* Most exceptions can be handled with interrupts turned on.
     We need to disable interrupts for page faults because the
     fault address is stored in CR2 and needs to be preserved. */
  intr_register_int (14, 0, INTR_OFF, page_fault, "#PF Page-Fault Exception");
}

/** Prints exception statistics. */
void
exception_print_stats (void) 
{
  printf ("Exception: %lld page faults\n", page_fault_cnt);
}

/** Handler for an exception (probably) caused by a user process. */
static void
kill (struct intr_frame *f) 
{
  /* This interrupt is one (probably) caused by a user process.
     For example, the process might have tried to access unmapped
     virtual memory (a page fault).  For now, we simply kill the
     user process.  Later, we'll want to handle page faults in
     the kernel.  Real Unix-like operating systems pass most
     exceptions back to the process via signals, but we don't
     implement them. */
     
  /* The interrupt frame's code segment value tells us where the
     exception originated. */
  switch (f->cs)
    {
    case SEL_UCSEG:
      /* User's code segment, so it's a user exception, as we
         expected.  Kill the user process.  */
      printf ("%s: dying due to interrupt %#04x (%s).\n",
              thread_name (), f->vec_no, intr_name (f->vec_no));
      intr_dump_frame (f);
      thread_exit (); 

    case SEL_KCSEG:
      /* Kernel's code segment, which indicates a kernel bug.
         Kernel code shouldn't throw exceptions.  (Page faults
         may cause kernel exceptions--but they shouldn't arrive
         here.)  Panic the kernel to make the point.  */
      intr_dump_frame (f);
      PANIC ("Kernel bug - unexpected interrupt in kernel"); 

    default:
      /* Some other code segment?  Shouldn't happen.  Panic the
         kernel. */
      printf ("Interrupt %#04x (%s) in unknown segment %04x\n",
             f->vec_no, intr_name (f->vec_no), f->cs);
      thread_exit ();
    }
}

/** Page fault handler.  This is a skeleton that must be filled in
   to implement virtual memory.  Some solutions to project 2 may
   also require modifying this code.

   At entry, the address that faulted is in CR2 (Control Register
   2) and information about the fault, formatted as described in
   the PF_* macros in exception.h, is in F's error_code member.  The
   example code here shows how to parse that information.  You
   can find more information about both of these in the
   description of "Interrupt 14--Page Fault Exception (#PF)" in
   [IA32-v3a] section 5.15 "Exception and Interrupt Reference". */
static void
page_fault (struct intr_frame *f) 
{
  bool not_present;  /**< True: not-present page, false: writing r/o page. */
  bool write;        /**< True: access was write, false: access was read. */
  bool user;         /**< True: access by user, false: access by kernel. */
  void *fault_addr;  /**< Fault address. */

  /* Obtain faulting address, the virtual address that was
     accessed to cause the fault.  It may point to code or to
     data.  It is not necessarily the address of the instruction
     that caused the fault (that's f->eip).
     See [IA32-v2a] "MOV--Move to/from Control Registers" and
     [IA32-v3a] 5.15 "Interrupt 14--Page Fault Exception
     (#PF)". */
  asm ("movl %%cr2, %0" : "=r" (fault_addr));

  /* Turn interrupts back on (they were only off so that we could
     be assured of reading CR2 before it changed). */
  intr_enable ();

  /* Count page faults. */
  page_fault_cnt++;

  /* Determine cause. */
  not_present = (f->error_code & PF_P) == 0;
  write = (f->error_code & PF_W) != 0;
  user = (f->error_code & PF_U) != 0;
  
  if (!is_user_vaddr(fault_addr)) {
    kill(f);
  }

  struct thread *t = thread_current();
  uint32_t *pte = lookup_page(t->pagedir, fault_addr, false);

  // no such page or not a lazy alloc page
  if (pte == NULL || (*pte & PTE_L) == 0) {
    // check stack growth
    ASSERT (t->esp != NULL);
    if ((uint32_t)fault_addr < ((uint32_t)t->esp - 32)) {
      // should not use kill()
      // maybe a syscall pagefault in kernel
      // kill() will stop the kernel
      t->esp = NULL;
      thread_exit();
    } else {
      void *kpage = palloc_get_page_for_page_fault();
      if (kpage == NULL) {
        kill (f);
      }
      memset(kpage, 0, PGSIZE);
      if (!pagedir_set_page(t->pagedir, pg_round_down(fault_addr), kpage, true)) {
        palloc_free_page(kpage);
        kill (f);
      }
      return;
    }
  }

  // pte != NULL && PTE_L set
  struct spt *spt_elem = (struct spt *)(*pte & (~0x3));
  bool writable;
  bool zero_init = (((*pte & PTE_SPT) >> 3) == 0); // PTE_L || (PTE_L | PTE_ZW)
  // check writable first, so that we can set page right after palloc
  if (zero_init) {
    writable = (*pte & PTE_ZW) != 0; 
    if (!writable && write) {
      thread_exit();
    }
  } else {
    writable = spt_elem->writable;
    if (!writable && write) {
      thread_exit();
    }
  }
  void *kpage = palloc_get_page_for_page_fault(); // assume all lazy pages are user pages
  if (kpage == NULL) {
    kill (f);
  }
  memset(kpage, 0, PGSIZE);
  
  // if zero_init, no more things to do
  if (!zero_init) {
    if(spt_elem->type == SPT_FILE) {
      struct file *file = spt_elem->file;
      uint32_t ptr_offset = (uint32_t)pg_round_down(fault_addr) - (uint32_t)spt_elem->start_uaddr;
      uint32_t size_to_read = spt_elem->nbytes - ptr_offset;
      if (size_to_read > PGSIZE)
        size_to_read = PGSIZE;
      uint32_t offset = spt_elem->offset + ptr_offset;
      file_seek(file, offset);
      if ((uint32_t)file_read(file, kpage, size_to_read) != size_to_read) {
        palloc_free_page(kpage);
        kill (f);
      }
    } else {
      swap_from_disk(kpage, spt_elem->pos);
    }
  }
  if (!pagedir_set_page(t->pagedir, pg_round_down(fault_addr), kpage, writable)) {
    palloc_free_page(kpage);
    kill (f);
  }
  return;
}

