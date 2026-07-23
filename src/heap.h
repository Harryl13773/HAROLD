#ifndef HEAP_H
#define HEAP_H

#include <stddef.h>

// Reserves the heap region and creates the first free block
void heap_init(void);

// Allocates at least `size` bytes, or NULL if nothing big enough is free
void *kmalloc(size_t size);

// Returns a previously allocated block back to the free list
void kfree(void *ptr);

#endif