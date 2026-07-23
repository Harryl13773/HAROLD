#ifndef PMM_H
#define PMM_H

#include <stdint.h>
#include "multiboot.h"

// Builds the frame bitmap from the multiboot memory map
void pmm_init(struct multiboot_info *info, uint32_t kernel_end);

// Returns a free 4KB frame's physical address, or 0 if out of memory
uint32_t pmm_alloc_frame(void);

// Marks a frame as free again
void pmm_free_frame(uint32_t frame_addr);

// Reports how many frames are currently free
uint32_t pmm_get_free_frame_count(void);

// Reports how many frames are usable in total
uint32_t pmm_get_total_frame_count(void);

#endif