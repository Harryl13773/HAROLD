// Public interface for the kernel heap allocator (kmalloc/kfree)

#ifndef HEAP_H
#define HEAP_H

#include <stddef.h>

#define HEAP_SIZE 0x100000 // 1MB — must match the reservation pmm_init is told about in kernel.c

// Reserves the heap region and creates the first free block
void heap_init(void);

// Allocates at least `size` bytes, or NULL if nothing big enough is free
void *kmalloc(size_t size);

// Returns a previously allocated block back to the free list
void kfree(void *ptr);

#endif