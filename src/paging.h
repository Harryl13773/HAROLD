// Public interface for paging setup and per-task page directory/table management.

#ifndef PAGING_H
#define PAGING_H

#define PAGE_SIZE 4096

// Identity-maps the first 4MB and enables paging
void paging_init(void);

// Returns the kernel's own page directory — every cloned directory starts as a copy of this
uint32_t *paging_get_kernel_directory(void);

// Allocates a fresh physical frame and copies the kernel's directory into it; returns NULL if no
// frame is free, or if the allocated frame falls outside the 4MB paging currently maps (see paging.c)
uint32_t *paging_clone_kernel_directory(void);

// Loads an arbitrary page directory into CR3 — the actual per-task address space switch
void paging_switch_directory(uint32_t *dir);

// Gives a directory its own private page table for vaddr's 4MB region, pre-populated with the
// kernel's own mappings — or returns the existing one if this directory already has one there.
// A process's own segments get remapped on top of this via paging_map_user_page below.
uint32_t *paging_get_or_create_user_table(uint32_t *dir, uint32_t vaddr);

// Maps one 4KB page in a private table to a physical frame, present/writable/user
void paging_map_user_page(uint32_t *table, uint32_t vaddr, uint32_t frame);

// Frees every private page table and private data frame a task's directory owns — everything
// that diverges from the kernel's own shared mappings. Safe to call on a directory that never
// diverged at all (nothing to free). Does not free the directory's own frame — task_reap does that.
void paging_free_user_directory(uint32_t *dir);

#endif