// TSS layout and public interface for installing it and updating the kernel stack pointer

#ifndef TSS_H
#define TSS_H

#include <stdint.h>

// Standard 32-bit TSS layout — only esp0/ss0/iomap_base are actually used
struct tss_entry
{
    uint32_t prev_tss;
    uint32_t esp0;
    uint32_t ss0;
    uint32_t esp1;
    uint32_t ss1;
    uint32_t esp2;
    uint32_t ss2;
    uint32_t cr3;
    uint32_t eip;
    uint32_t eflags;
    uint32_t eax, ecx, edx, ebx;
    uint32_t esp, ebp, esi, edi;
    uint32_t es, cs, ss, ds, fs, gs;
    uint32_t ldt;
    uint16_t trap;
    uint16_t iomap_base;
} __attribute__((packed));

extern struct tss_entry tss;

// Fills in the TSS and loads the task register
void tss_install(void);

// Updates the kernel stack used on the next ring3->ring0 transition
void tss_set_kernel_stack(uint32_t esp0);

#endif