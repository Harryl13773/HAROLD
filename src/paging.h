// Public interface for paging setup and per-task page directory/table management.

#ifndef PAGING_H
#define PAGING_H

#define PAGE_SIZE 4096

// Identity-maps the first 4MB and enables paging
void paging_init(void);

// Returns the kernel page directory used as the base for clones
uint32_t *paging_get_kernel_directory(void);

// Clones the kernel page directory into a new frame; returns NULL on failure
uint32_t *paging_clone_kernel_directory(void);

// Switches to a page directory via CR3
void paging_switch_directory(uint32_t *dir);

// Gets or creates a private user page table for vaddr's 4MB region
uint32_t *paging_get_or_create_user_table(uint32_t *dir, uint32_t vaddr);

// Maps a 4KB user page to a physical frame
void paging_map_user_page(uint32_t *table, uint32_t vaddr, uint32_t frame);

// Frees a directory's private user page tables and frames, but not the directory itself
void paging_free_user_directory(uint32_t *dir);

#endif