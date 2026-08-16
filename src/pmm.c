// Physical memory manager: bitmap-based frame allocator built from the multiboot memory map.

#include <stdint.h>
#include "pmm.h"
#include "terminal.h"
#include "io.h"

#define FRAME_SIZE 4096
#define MAX_FRAMES (1024 * 1024) // supports up to 4GB of RAM

// One bit per frame: 1 = used, 0 = free
static uint32_t frame_bitmap[MAX_FRAMES / 32];
static uint32_t total_frames = 0;
static uint32_t used_frames = 0;

// Marks a frame as used
static void bitmap_set(uint32_t frame)
{
    frame_bitmap[frame / 32] |= (1 << (frame % 32));
}

// Marks a frame as free
static void bitmap_clear(uint32_t frame)
{
    frame_bitmap[frame / 32] &= ~(1 << (frame % 32));
}

// Checks whether a frame is currently used
static int bitmap_test(uint32_t frame)
{
    return frame_bitmap[frame / 32] & (1 << (frame % 32));
}

// Parses the memory map, marks usable RAM free, and reclaims the kernel's own frames
void pmm_init(struct multiboot_info *info, uint32_t kernel_end)
{
    // Refuse to proceed without a real memory map
    if (!(info->flags & (1 << 6)))
    {
        terminal_writestring("PMM: no memory map available, halting\n");
        while (1)
        {
            __asm__ volatile("hlt");
        }
    }

    // Default everything to "used" until proven usable
    for (uint32_t i = 0; i < MAX_FRAMES / 32; i++)
    {
        frame_bitmap[i] = 0xFFFFFFFF;
    }

    struct mmap_entry *entry = (struct mmap_entry *)info->mmap_addr;
    uint32_t mmap_end = info->mmap_addr + info->mmap_length;

    // Walk GRUB's memory map and free every usable region
    while ((uint32_t)entry < mmap_end)
    {
        if (entry->type == 1)
        {
            uint32_t start_frame = (uint32_t)(entry->addr) / FRAME_SIZE;
            uint32_t frame_count = (uint32_t)(entry->len) / FRAME_SIZE;

            for (uint32_t i = 0; i < frame_count; i++)
            {
                uint32_t frame = start_frame + i;
                if (frame < MAX_FRAMES)
                {
                    bitmap_clear(frame);
                    total_frames++;
                }
            }
        }

        entry = (struct mmap_entry *)((uint32_t)entry + entry->size + sizeof(uint32_t));
    }

    // Never hand out frames the kernel itself occupies
    uint32_t kernel_end_frame = (kernel_end / FRAME_SIZE) + 1;
    for (uint32_t frame = 0; frame < kernel_end_frame; frame++)
    {
        if (!bitmap_test(frame))
        {
            bitmap_set(frame);
            total_frames--;
        }
    }

    used_frames = 0;
}

// Finds and reserves the first free frame. Self-protected like heap.c's kmalloc/fat.c's fat_open
// — this bitmap is shared across every task and interrupt context that ever allocates memory
// (task.c, paging.c, elf.c, usermode.c), so every caller needs this atomic regardless of whether
// it also wraps its own call site; nesting save_and_disable_interrupts is safe (see io.h).
uint32_t pmm_alloc_frame(void)
{
    uint32_t flags = save_and_disable_interrupts();

    uint32_t result = 0;
    for (uint32_t i = 0; i < total_frames + used_frames; i++)
    {
        if (!bitmap_test(i))
        {
            bitmap_set(i);
            used_frames++;
            result = i * FRAME_SIZE;
            break;
        }
    }

    restore_interrupts(flags);
    return result; // 0 means out of memory
}

// Releases a frame back to the free pool
void pmm_free_frame(uint32_t frame_addr)
{
    uint32_t flags = save_and_disable_interrupts();

    uint32_t frame = frame_addr / FRAME_SIZE;
    if (bitmap_test(frame))
    {
        bitmap_clear(frame);
        used_frames--;
    }
    // double-free is ignored, not treated as an error

    restore_interrupts(flags);
}

// Returns the current free frame count
uint32_t pmm_get_free_frame_count(void)
{
    return total_frames - used_frames;
}

// Returns the total usable frame count
uint32_t pmm_get_total_frame_count(void)
{
    return total_frames;
}