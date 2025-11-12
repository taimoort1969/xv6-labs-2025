// Physical memory allocator.
//
// - normal 4KB allocator (kalloc/kfree)
// - plus a tiny pool of 2MB “superpages” that vm.c can ask for

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

// If the lab headers don't define these, define them here.
// 2MB superpage and keep a small number of them.
#ifndef SUPERPAGE_SIZE
#define SUPERPAGE_SIZE (2 * 1024 * 1024)   // 2MB
#endif

#ifndef N_SUPERPAGES
#define N_SUPERPAGES 8                     // enough for the tests
#endif

extern char end[];   // first address after kernel, defined by kernel.ld

// ---------------------------------------------------------------------
// 4KB allocator state
// ---------------------------------------------------------------------

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

// ---------------------------------------------------------------------
// 2MB pool for superpages
// ---------------------------------------------------------------------
static void *super_free_list[N_SUPERPAGES];
static int   super_free_cnt = 0;

static void freerange(void *vstart, void *vend);

// ---------------------------------------------------------------------
// init
// ---------------------------------------------------------------------
void
kinit(void)
{
  initlock(&kmem.lock, "kmem");
  // carve up all physical memory once
  freerange(end, (void *)PHYSTOP);
}

// walk [vstart, vend) page by page.
// whenever we hit a 2MB-aligned chunk and still have room, keep that whole
// 2MB for superalloc(). everything else becomes a normal 4KB page.
static void
freerange(void *vstart, void *vend)
{
  char *p = (char *)PGROUNDUP((uint64)vstart);

  while ((uint64)p + PGSIZE <= (uint64)vend) {

    if (super_free_cnt < N_SUPERPAGES &&
        (((uint64)p & (SUPERPAGE_SIZE - 1)) == 0) &&
        (uint64)p + SUPERPAGE_SIZE <= (uint64)vend) {

      // stash this 2MB chunk
      super_free_list[super_free_cnt++] = p;
      p += SUPERPAGE_SIZE;
      continue;
    }

    // otherwise: give 4KB to normal allocator
    kfree(p);
    p += PGSIZE;
  }
}

// ---------------------------------------------------------------------
// 2MB API used by vm.c
// ---------------------------------------------------------------------
void *
superalloc(void)
{
  if (super_free_cnt == 0)
    return 0;
  return super_free_list[--super_free_cnt];
}

void
superfree(void *pa)
{
  if (super_free_cnt >= N_SUPERPAGES)
    panic("superfree: overflow");
  super_free_list[super_free_cnt++] = pa;
}

// ---------------------------------------------------------------------
// normal 4KB allocator
// ---------------------------------------------------------------------
void
kfree(void *pa)
{
  struct run *r;

  if (((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // fill with junk to catch dangling refs
  memset(pa, 1, PGSIZE);

  r = (struct run *)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if (r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if (r)
    memset((char *)r, 5, PGSIZE);  // mark allocated
  return (void *)r;
}
