#include <stdint.h>
#include "paging.h"
#include "terminal.h"

#define PAGE_PRESENT 0x1
#define PAGE_WRITABLE 0x2
#define PAGE_USER 0x4
#define ENTRIES 1024
#define PAGE_SIZE 4096

// Both must be page-aligned — low 12 bits of the address double as flag bits
static uint32_t page_directory[ENTRIES] __attribute__((aligned(PAGE_SIZE)));
static uint32_t first_page_table[ENTRIES] __attribute__((aligned(PAGE_SIZE)));

// Tells the CPU where the page directory lives
static inline void load_page_directory(uint32_t *dir)
{
    __asm__ volatile("mov %0, %%cr3" : : "r"(dir));
}

// Flips the PG bit in CR0 — the instant this runs, every address is translated
static inline void enable_paging(void)
{
    uint32_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));
}

void paging_init(void)
{
    // PAGE_USER makes the whole 4MB ring-3 accessible — not real isolation yet, just enough to prove ring 3 works
    for (uint32_t i = 0; i < ENTRIES; i++)
    {
        first_page_table[i] = (i * PAGE_SIZE) | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
    }

    // Every other 4MB region stays unmapped — touching it will correctly page-fault
    for (uint32_t i = 0; i < ENTRIES; i++)
    {
        page_directory[i] = 0;
    }

    page_directory[0] = (uint32_t)first_page_table | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;

    load_page_directory(page_directory);
    enable_paging();

    terminal_writestring("Paging enabled: first 4MB identity-mapped\n");
}