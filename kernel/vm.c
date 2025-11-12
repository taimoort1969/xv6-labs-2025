#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "defs.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"
#include "vm.h"

#ifndef SUPERPAGE_SIZE
#define SUPERPAGE_SIZE (2 * 1024 * 1024)
#endif

#ifndef SUPERPAGE_NPAGES
#define SUPERPAGE_NPAGES (SUPERPAGE_SIZE / PGSIZE)
#endif

extern void *superalloc(void);
extern void superfree(void *);   // <-- correct prototype

pagetable_t kernel_pagetable;

extern char etext[];
extern char trampoline[];

/* ---------------- kernel pagetable ---------------- */

pagetable_t
kvmmake(void)
{
  pagetable_t kpgtbl = (pagetable_t)kalloc();
  if(kpgtbl == 0)
    panic("kvmmake");
  memset(kpgtbl, 0, PGSIZE);

  kvmmap(kpgtbl, UART0, UART0, PGSIZE, PTE_R | PTE_W);
  kvmmap(kpgtbl, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);
#ifdef LAB_NET
  kvmmap(kpgtbl, 0x30000000L, 0x30000000L, 0x10000000, PTE_R | PTE_W);
  kvmmap(kpgtbl, 0x40000000L, 0x40000000L, 0x20000, PTE_R | PTE_W);
#endif
  kvmmap(kpgtbl, PLIC, PLIC, 0x4000000, PTE_R | PTE_W);
  kvmmap(kpgtbl, KERNBASE, KERNBASE, (uint64)etext - KERNBASE, PTE_R | PTE_X);
  kvmmap(kpgtbl, (uint64)etext, (uint64)etext, PHYSTOP - (uint64)etext, PTE_R | PTE_W);
  kvmmap(kpgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);
  proc_mapstacks(kpgtbl);
  return kpgtbl;
}

void
kvminit(void)
{
  kernel_pagetable = kvmmake();
}

void
kvminithart(void)
{
  sfence_vma();
  w_satp(MAKE_SATP(kernel_pagetable));
  sfence_vma();
}

/* ---------------- walking helpers ---------------- */

pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc)
{
  if(va >= MAXVA)
    panic("walk");

  for(int level = 2; level > 0; level--) {
    pte_t *pte = &pagetable[PX(level, va)];
    if(*pte & PTE_V) {
#ifdef LAB_PGTBL
      if (PTE_LEAF(*pte))
        return pte;
#endif
      pagetable = (pagetable_t)PTE2PA(*pte);
    } else {
      if(!alloc)
        return 0;
      pagetable_t newpt = (pagetable_t)kalloc();
      if(newpt == 0)
        return 0;
      memset(newpt, 0, PGSIZE);
      *pte = PA2PTE(newpt) | PTE_V;
      pagetable = newpt;
    }
  }
  return &pagetable[PX(0, va)];
}

static pte_t *
walk_for_level(pagetable_t pagetable, uint64 va, int alloc, int want_level)
{
  for (int level = 2; level > want_level; level--) {
    pte_t *pte = &pagetable[PX(level, va)];
    if (*pte & PTE_V) {
      pagetable = (pagetable_t)PTE2PA(*pte);
    } else {
      if (!alloc)
        return 0;
      pagetable_t newpt = (pagetable_t)kalloc();
      if (newpt == 0)
        return 0;
      memset(newpt, 0, PGSIZE);
      *pte = PA2PTE(newpt) | PTE_V;
      pagetable = newpt;
    }
  }
  return &pagetable[PX(want_level, va)];
}

uint64
walkaddr(pagetable_t pagetable, uint64 va)
{
  if (va >= MAXVA)
    return 0;
  pte_t *pte = walk(pagetable, va, 0);
  if (pte == 0 || (*pte & PTE_V) == 0 || (*pte & PTE_U) == 0)
    return 0;
  return PTE2PA(*pte);
}

/* ---------------- vmprint ---------------- */

#ifdef LAB_PGTBL
static void
vmprint_walk(pagetable_t pagetable, int depth, uint64 baseva)
{
  for (int i = 0; i < 512; i++) {
    pte_t pte = pagetable[i];
    if ((pte & PTE_V) == 0)
      continue;

    uint64 va = baseva | ((uint64)i << (PXSHIFT(depth)));
    uint64 pa = PTE2PA(pte);

    for (int d = 0; d < depth; d++)
      printf(" ..");
    printf("%p: pte %p pa %p\n", (void *)va, (void *)pte, (void *)pa);

    if ((pte & (PTE_R|PTE_W|PTE_X)) == 0 && depth > 0)
      vmprint_walk((pagetable_t)pa, depth - 1, va);
  }
}

void
vmprint(pagetable_t pagetable)
{
  printf("page table %p\n", (void *)pagetable);
  vmprint_walk(pagetable, 2, 0);
}
#endif

/* ---------------- mapping ---------------- */

void
kvmmap(pagetable_t kpgtbl, uint64 va, uint64 pa, uint64 sz, int perm)
{
  if (mappages(kpgtbl, va, sz, pa, perm) != 0)
    panic("kvmmap");
}

int
mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
  if ((va % PGSIZE) != 0)
    panic("mappages: va not aligned");
  if ((size % PGSIZE) != 0)
    panic("mappages: size not aligned");
  if (size == 0)
    panic("mappages: size 0");

  uint64 a = va;
  uint64 last = va + size - PGSIZE;

  for (;;) {
    pte_t *pte = walk(pagetable, a, 1);
    if (pte == 0)
      return -1;
    if (*pte & PTE_V)
      panic("mappages: remap");
    *pte = PA2PTE(pa) | perm | PTE_V;
    if (a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}


int
mappages_super(pagetable_t pagetable, uint64 va, uint64 pa, int perm)
{
  if ((va & (SUPERPAGE_SIZE - 1)) || (pa & (SUPERPAGE_SIZE - 1)))
    return -1;

  pte_t *l1pte = walk_for_level(pagetable, va, 1, 1);
  if (l1pte == 0)
    return -1;
  if (*l1pte & PTE_V)
    return -1;

  *l1pte = PA2PTE(pa) | perm | PTE_V;
  return 0;
}

pagetable_t
uvmcreate(void)
{
  pagetable_t pagetable = (pagetable_t)kalloc();
  if (pagetable == 0)
    return 0;
  memset(pagetable, 0, PGSIZE);
  return pagetable;
}


static int
demote_superpage(pagetable_t pagetable, uint64 va)
{
  pte_t *l1pte = walk_for_level(pagetable, va, 0, 1);
  if (l1pte == 0 || (*l1pte & PTE_V) == 0)
    return -1;
  if ((*l1pte & (PTE_R|PTE_W|PTE_X)) == 0)
    return -1;

  uint64 basepa = PTE2PA(*l1pte);
  uint flags = PTE_FLAGS(*l1pte);

  pagetable_t newpt = (pagetable_t)kalloc();
  if (newpt == 0)
    return -1;
  memset(newpt, 0, PGSIZE);

  for (int i = 0; i < SUPERPAGE_NPAGES; i++)
    newpt[i] = PA2PTE(basepa + (uint64)i * PGSIZE) | flags | PTE_V;

  *l1pte = PA2PTE(newpt) | PTE_V;
  return 0;
}

int
ismapped(pagetable_t pagetable, uint64 va)
{
  pte_t *pte = walk(pagetable, va, 0);
  return pte && (*pte & PTE_V);
}

/* ---------------- unmap (superpage aware) ---------------- */

void
uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  if (va % PGSIZE)
    panic("uvmunmap: not aligned");

  uint64 end = va + npages * PGSIZE;

  for (uint64 a = va; a < end; a += PGSIZE) {
    pte_t *l1pte = walk_for_level(pagetable, a, 0, 1);
    if (l1pte && (*l1pte & PTE_V) && (*l1pte & (PTE_R|PTE_W|PTE_X))) {
      if ((a % SUPERPAGE_SIZE) == 0 && (end - a) >= SUPERPAGE_SIZE) {
        if (do_free)
          superfree((void *)PTE2PA(*l1pte));
        *l1pte = 0;
        a += SUPERPAGE_SIZE - PGSIZE;
        continue;
      } else {
        if (demote_superpage(pagetable, a) != 0)
          panic("uvmunmap: demote failed");
      }
    }

    pte_t *pte = walk(pagetable, a, 0);
    if (pte == 0 || (*pte & PTE_V) == 0)
      continue;
    if (do_free)
      kfree((void *)PTE2PA(*pte));
    *pte = 0;
  }
}

/* ---------------- allocate user mem ---------------- */

uint64
uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz, int xperm)
{
  if (newsz < oldsz)
    return oldsz;

  oldsz = PGROUNDUP(oldsz);

  for (uint64 a = oldsz; a < newsz; ) {
    /* try 2MB first */
    if ((a % SUPERPAGE_SIZE) == 0 && (newsz - a) >= SUPERPAGE_SIZE) {
      void *big = superalloc();
      if (big && mappages_super(pagetable, a, (uint64)big,
                                PTE_R | PTE_W | PTE_U | xperm) == 0) {
        a += SUPERPAGE_SIZE;
        continue;
      }
      if (big)
        superfree(big);
    }

    /* fall back to 4KB */
    void *mem = kalloc();
    if (mem == 0) {
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
#ifndef LAB_SYSCALL
    memset(mem, 0, PGSIZE);
#endif
    if (mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_R | PTE_U | xperm) != 0) {
      kfree(mem);
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
    a += PGSIZE;
  }

  return newsz;
}

uint64
uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  if (newsz >= oldsz)
    return oldsz;
  if (PGROUNDUP(newsz) < PGROUNDUP(oldsz)) {
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);
  }
  return newsz;
}

/* ---------------- free page-table tree ---------------- */

void
freewalk(pagetable_t pagetable)
{
  for (int i = 0; i < 512; i++) {
    pte_t pte = pagetable[i];
    if ((pte & PTE_V) && (pte & (PTE_R | PTE_W | PTE_X)) == 0) {
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t)child);
      pagetable[i] = 0;
    } else if (pte & PTE_V) {
      panic("freewalk: leaf");
    }
  }
  kfree((void *)pagetable);
}

void
uvmfree(pagetable_t pagetable, uint64 sz)
{
  if (sz > 0)
    uvmunmap(pagetable, 0, PGROUNDUP(sz) / PGSIZE, 1);
  freewalk(pagetable);
}

/* ---------------- copy address space (superpage aware) ---------------- */

int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  uint64 i;
  for (i = 0; i < sz; i += PGSIZE) {
    pte_t *l1pte = walk_for_level(old, i, 0, 1);
    if (l1pte && (*l1pte & PTE_V) && (*l1pte & (PTE_R|PTE_W|PTE_X))) {
      if ((i % SUPERPAGE_SIZE) == 0 && sz - i >= SUPERPAGE_SIZE) {
        uint64 pa = PTE2PA(*l1pte);
        uint flags = PTE_FLAGS(*l1pte);
        void *blk = superalloc();
        if (blk) {
          memmove(blk, (void *)pa, SUPERPAGE_SIZE);
          if (mappages_super(new, i, (uint64)blk, flags) != 0) {
            superfree(blk);
            goto err;
          }
          i += SUPERPAGE_SIZE - PGSIZE;
          continue;
        }
      }
    }

    pte_t *pte = walk(old, i, 0);
    if (pte == 0 || (*pte & PTE_V) == 0)
      continue;
    uint64 pa = PTE2PA(*pte);
    uint flags = PTE_FLAGS(*pte);
    char *mem = kalloc();
    if (mem == 0)
      goto err;
    memmove(mem, (char *)pa, PGSIZE);
    if (mappages(new, i, PGSIZE, (uint64)mem, flags) != 0) {
      kfree(mem);
      goto err;
    }
  }
  return 0;

err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}

/* ---------------- misc ---------------- */

void
uvmclear(pagetable_t pagetable, uint64 va)
{
  pte_t *pte = walk(pagetable, va, 0);
  if (pte == 0)
    panic("uvmclear");
  *pte &= ~PTE_U;
}

int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  while (len > 0) {
    uint64 va0 = PGROUNDDOWN(dstva);
    if (va0 >= MAXVA)
      return -1;
    uint64 pa0 = walkaddr(pagetable, va0);
    if (pa0 == 0) {
      if ((pa0 = vmfault(pagetable, va0, 0)) == 0)
        return -1;
    }
    pte_t *pte = walk(pagetable, va0, 0);
    if (pte == 0 || (*pte & PTE_W) == 0)
      return -1;
    uint64 n = PGSIZE - (dstva - va0);
    if (n > len)
      n = len;
    memmove((void *)(pa0 + (dstva - va0)), src, n);
    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}

int
copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
  while (len > 0) {
    uint64 va0 = PGROUNDDOWN(srcva);
    uint64 pa0 = walkaddr(pagetable, va0);
    if (pa0 == 0) {
      if ((pa0 = vmfault(pagetable, va0, 0)) == 0)
        return -1;
    }
    uint64 n = PGSIZE - (srcva - va0);
    if (n > len)
      n = len;
    memmove(dst, (void *)(pa0 + (srcva - va0)), n);
    len -= n;
    dst += n;
    srcva = va0 + PGSIZE;
  }
  return 0;
}

int
copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
  int got_null = 0;
  while (!got_null && max > 0) {
    uint64 va0 = PGROUNDDOWN(srcva);
    uint64 pa0 = walkaddr(pagetable, va0);
    if (pa0 == 0)
      return -1;
    uint64 n = PGSIZE - (srcva - va0);
    if (n > max)
      n = max;
    char *p = (char *)(pa0 + (srcva - va0));
    while (n > 0) {
      if (*p == '\0') {
        *dst = '\0';
        got_null = 1;
        break;
      } else {
        *dst = *p;
      }
      --n;
      --max;
      p++;
      dst++;
    }
    srcva = va0 + PGSIZE;
  }
  return got_null ? 0 : -1;
}

uint64
vmfault(pagetable_t pagetable, uint64 va, int read)
{
  struct proc *p = myproc();
  if (va >= p->sz)
    return 0;
  va = PGROUNDDOWN(va);
  if (ismapped(pagetable, va))
    return 0;
  uint64 mem = (uint64)kalloc();
  if (mem == 0)
    return 0;
  memset((void *)mem, 0, PGSIZE);
  if (mappages(p->pagetable, va, PGSIZE, mem, PTE_W | PTE_U | PTE_R) != 0) {
    kfree((void *)mem);
    return 0;
  }
  return mem;
}

#ifdef LAB_PGTBL
pte_t *
pgpte(pagetable_t pagetable, uint64 va)
{
  return walk(pagetable, va, 0);
}
#endif
