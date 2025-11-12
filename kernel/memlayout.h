#ifndef MEMLAYOUT_H
#define MEMLAYOUT_H

// Physical memory layout
//
// qemu -machine virt is set up like this,
// based on https://github.com/qemu/qemu/blob/master/hw/riscv/virt.c
//
// 00001000 -- boot ROM, provided by qemu
// 02000000 -- UART0 registers
// 0x10001000 -- virtio disk interface
// 0x0c000000 -- PLIC
// 0x80000000 -- start of kernel
// 0x88000000 -- end of RAM (PHYSTOP)

#define UART0     0x10000000L
#define VIRTIO0   0x10001000L
#define PLIC      0x0c000000L

// PLIC memory-mapped register access macros
#define PLIC_PRIORITY     (PLIC + 0x0)
#define PLIC_PENDING      (PLIC + 0x1000)
#define PLIC_MENABLE(hart)   (PLIC + 0x2000 + (hart) * 0x100)
#define PLIC_SENABLE(hart)   (PLIC + 0x2080 + (hart) * 0x100)
#define PLIC_MPRIORITY(hart) (PLIC + 0x200000 + (hart) * 0x2000)
#define PLIC_SPRIORITY(hart) (PLIC + 0x201000 + (hart) * 0x2000)
#define PLIC_MCLAIM(hart)    (PLIC + 0x200004 + (hart) * 0x2000)
#define PLIC_SCLAIM(hart)    (PLIC + 0x201004 + (hart) * 0x2000)

#define UART0_IRQ   10
#define VIRTIO0_IRQ 1

#define KERNBASE  0x80000000L
#define PHYSTOP   (KERNBASE + 128*1024*1024)

// map the trampoline page to the highest address,
// in both user and kernel space.
#define TRAMPOLINE (MAXVA - PGSIZE)

// map kernel stacks beneath the trampoline,
// each surrounded by invalid guard pages.
#define KSTACK(p) (TRAMPOLINE - ((p)+1)*2*PGSIZE)

// User memory layout.
// Address zero first:
//   text
//   original data and bss
//   fixed-size stack
//   expandable heap
//   ...
//   USYSCALL (shared with kernel)
//   TRAPFRAME (p->trapframe, used by the trampoline)
//   TRAMPOLINE (the same page as in the kernel)
#define TRAPFRAME (TRAMPOLINE - PGSIZE)
#define USYSCALL  (TRAPFRAME - PGSIZE)

#ifndef __ASSEMBLER__
struct usyscall {
  int pid;  // Process ID
};
#endif  // __ASSEMBLER__

#endif  // MEMLAYOUT_H

