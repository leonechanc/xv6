// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

// struct {
//   struct spinlock lock;
//   struct run *freelist;
// } kmem;

struct kmem{
  struct spinlock lock;
  struct run *freelist;
};
struct kmem km[NCPU];
char lkname[NCPU][16];

void
kinit()
{
  for (int i = 0; i < NCPU; i++) {
    snprintf(lkname[i], sizeof(lkname[i]), "kmem_cpu_%d", i);
    initlock(&km[i].lock, lkname[i]);
  }
  freerange(end, (void*)PHYSTOP);
}

// void
// kinit()
// {
//   initlock(&kmem.lock, "kmem");
//   freerange(end, (void*)PHYSTOP);
// }

void kfree_cpu(void* pa, int cpu) {
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;
  acquire(&km[cpu].lock);
  r->next = km[cpu].freelist;
  km[cpu].freelist = r;
  release(&km[cpu].lock);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  int cpu;

  p = (char*)PGROUNDUP((uint64)pa_start);
  push_off();
  cpu = cpuid();
  pop_off();
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree_cpu(p, cpu); // Let freerange give all free memory to the CPU running freerange.
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  int cpu;
  push_off();
  cpu = cpuid();
  pop_off();
  kfree_cpu(pa, cpu);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;
  int cpu;

  push_off();
  cpu = cpuid();
  pop_off();

  acquire(&km[cpu].lock);
  r = km[cpu].freelist;
  if(r) {
    km[cpu].freelist = r->next;
    release(&km[cpu].lock);
    memset((char*)r, 5, PGSIZE); // fill with junk
  } else {
    release(&km[cpu].lock);
    for (int i = 0; i < NCPU; i++) {
      if (i == cpu) 
        continue;
      acquire(&km[i].lock);
      r = km[i].freelist;
      if (r) {
        km[i].freelist = r->next;
        release(&km[i].lock);
        memset((char*)r, 5, PGSIZE); // fill with junk
        break;
      }
      release(&km[i].lock);
    }
  }
    
  return (void*)r;
}
