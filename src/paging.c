// Paging setup and per-task page directory/table management, including private user page mapping and teardown

#include <stdint.h>
#include <stddef.h>
#include "paging.h"
#include "pmm.h"
#include "terminal.h"

#define PAGE_PRESENT 0x1
#define PAGE_WRITABLE 0x2
#define PAGE_USER 0x4
#define ENTRIES 1024

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

// Returns the kernel's own page directory — every cloned directory starts as a copy of this
uint32_t *paging_get_kernel_directory(void)
{
    return page_directory;
}

// Allocates a fresh physical frame and copies the kernel's directory into it
uint32_t *paging_clone_kernel_directory(void)
{
    uint32_t phys = pmm_alloc_frame();

    terminal_writestring("Paging: clone got raw frame ");
    terminal_print_hex(phys);
    terminal_writestring(", free frames now ");
    terminal_print_dec(pmm_get_free_frame_count());
    terminal_writestring("\n");

    if (phys == 0)
    {
        return NULL; // out of memory
    }

    // Paging only maps the first 4MB right now (see paging_init above) — a frame beyond that is
    // physically real but has no mapping yet, so treating it as a directly-usable pointer would
    // silently corrupt whatever's actually at that virtual address. Refuse rather than risk it.
    if (phys >= 0x400000)
    {
        pmm_free_frame(phys);
        terminal_writestring("Paging: clone failed, allocated frame is outside the mapped 4MB\n");
        return NULL;
    }

    uint32_t *new_dir = (uint32_t *)phys; // identity-mapped here, so physical address doubles as the pointer
    for (uint32_t i = 0; i < ENTRIES; i++)
    {
        new_dir[i] = page_directory[i];
    }

    return new_dir;
}

// Loads an arbitrary page directory into CR3 — the actual per-task address space switch
void paging_switch_directory(uint32_t *dir)
{
    load_page_directory(dir);
}

// Allocates a fresh page table pre-populated with the kernel's own mappings — the starting point
// for a process's private view of one 4MB region, before its own segments get remapped on top
static uint32_t *paging_alloc_user_page_table(void)
{
    uint32_t phys = pmm_alloc_frame();

    terminal_writestring("Paging: table got raw frame ");
    terminal_print_hex(phys);
    terminal_writestring(", free frames now ");
    terminal_print_dec(pmm_get_free_frame_count());
    terminal_writestring("\n");

    if (phys == 0)
    {
        return NULL; // out of memory
    }

    if (phys >= 0x400000) // same reasoning as paging_clone_kernel_directory — see the comment there
    {
        pmm_free_frame(phys);
        terminal_writestring("Paging: page table alloc failed, frame outside the mapped 4MB\n");
        return NULL;
    }

    uint32_t *table = (uint32_t *)phys;
    for (uint32_t i = 0; i < ENTRIES; i++)
    {
        table[i] = first_page_table[i]; // start as an exact copy of the kernel's own mappings
    }

    return table;
}

// Gives a directory its own private page table for vaddr's 4MB region, or returns the existing
// one if a previous call already created it (so loading multiple PT_LOAD segments of the same
// ELF, all within the same 4MB region, doesn't allocate a second table by mistake)
uint32_t *paging_get_or_create_user_table(uint32_t *dir, uint32_t vaddr)
{
    uint32_t dir_index = vaddr / (PAGE_SIZE * ENTRIES);
    if (dir_index >= ENTRIES)
    {
        return NULL; // out of range — shouldn't happen for anything this project actually loads
    }

    uint32_t entry = dir[dir_index];
    uint32_t *existing_table = (uint32_t *)(entry & ~0xFFF);
    if ((entry & PAGE_PRESENT) && existing_table != first_page_table)
    {
        return existing_table; // already has its own private table here — reuse it
    }

    uint32_t *table = paging_alloc_user_page_table();
    if (table == NULL)
    {
        return NULL;
    }

    dir[dir_index] = (uint32_t)table | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
    return table;
}

// Maps one 4KB page in a private table to a physical frame, present/writable/user
void paging_map_user_page(uint32_t *table, uint32_t vaddr, uint32_t frame)
{
    uint32_t index = (vaddr / PAGE_SIZE) % ENTRIES;
    table[index] = frame | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
}

// Frees every private page table and private data frame a task's directory owns
void paging_free_user_directory(uint32_t *dir)
{
    for (uint32_t i = 0; i < ENTRIES; i++)
    {
        uint32_t entry = dir[i];
        if (!(entry & PAGE_PRESENT))
        {
            continue;
        }

        uint32_t *table = (uint32_t *)(entry & ~0xFFF);
        if (table == first_page_table)
        {
            continue; // still a shared kernel mapping — not this task's to free
        }

        for (uint32_t j = 0; j < ENTRIES; j++)
        {
            uint32_t table_entry = table[j];
            if (!(table_entry & PAGE_PRESENT))
            {
                continue;
            }
            if (table_entry == first_page_table[j])
            {
                continue; // untouched copy of the kernel's own mapping — not privately owned
            }

            pmm_free_frame(table_entry & ~0xFFF);
        }

        pmm_free_frame((uint32_t)table);
    }
}