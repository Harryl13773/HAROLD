#include <stdint.h>
#include <stddef.h>
#include "gdt.h"
#include "tss.h"

struct tss_entry tss;

// Bootstrap stack used only before the first task switch — later tasks get their own via tss_set_kernel_stack
static uint8_t tss_kernel_stack[4096] __attribute__((aligned(16)));

// Loads the task register with the TSS selector from the GDT
static void tss_flush(void)
{
    __asm__ volatile("ltr %%ax" : : "a"((uint16_t)GDT_TSS));
}

void tss_install(void)
{
    // Explicitly zeroed rather than relying on .bss being pre-cleared
    for (size_t i = 0; i < sizeof(tss); i++)
    {
        ((uint8_t *)&tss)[i] = 0;
    }

    tss.ss0 = GDT_KERNEL_DATA;
    tss.esp0 = (uint32_t)(tss_kernel_stack + sizeof(tss_kernel_stack));
    tss.iomap_base = sizeof(tss); // no I/O bitmap — ring 3 port I/O always faults

    tss_flush();
}

void tss_set_kernel_stack(uint32_t esp0)
{
    tss.esp0 = esp0;
}