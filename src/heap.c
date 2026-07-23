#include <stdint.h>
#include <stddef.h>
#include "heap.h"
#include "terminal.h"

#define HEAP_SIZE 0x100000 // 1MB — comfortably inside the identity-mapped 4MB
#define ALIGNMENT 8        // keeps every returned pointer 8-byte aligned
#define MIN_SPLIT 16       // don't bother splitting off a sliver smaller than this

extern uint32_t kernel_end; // linker symbol marking the end of kernel code/data

// One block's metadata, stored immediately before its payload
struct block_header
{
    size_t size;
    int free;
    struct block_header *next;
    struct block_header *prev;
};

static struct block_header *heap_start = NULL;
static int heap_ready = 0;

// Rounds a size up to the nearest multiple of ALIGNMENT
static size_t align_up(size_t size)
{
    return (size + (ALIGNMENT - 1)) & ~(ALIGNMENT - 1);
}

// Sets up the heap as one big free block starting right after the kernel
void heap_init(void)
{
    uint32_t start = ((uint32_t)&kernel_end + (ALIGNMENT - 1)) & ~(ALIGNMENT - 1);

    heap_start = (struct block_header *)start;
    heap_start->size = HEAP_SIZE - sizeof(struct block_header);
    heap_start->free = 1;
    heap_start->next = NULL;
    heap_start->prev = NULL;

    heap_ready = 1;

    terminal_writestring("Heap initialized\n");
}

// Breaks a block in two if the leftover space is worth keeping as its own free block
static void split_block(struct block_header *block, size_t size)
{
    if (block->size < size + sizeof(struct block_header) + MIN_SPLIT)
    {
        return; // leftover too small to be useful — hand over the whole block
    }

    struct block_header *new_block = (struct block_header *)((uint8_t *)(block + 1) + size);

    new_block->size = block->size - size - sizeof(struct block_header);
    new_block->free = 1;
    new_block->next = block->next;
    new_block->prev = block;

    if (block->next != NULL)
    {
        block->next->prev = new_block;
    }

    block->next = new_block;
    block->size = size;
}

// First-fit search through the block list for a free block big enough for size
void *kmalloc(size_t size)
{
    if (!heap_ready || size == 0)
    {
        return NULL;
    }

    size = align_up(size);
    struct block_header *block = heap_start;

    while (block != NULL)
    {
        if (block->free && block->size >= size)
        {
            split_block(block, size);
            block->free = 0;
            return (void *)(block + 1); // payload starts right after the header
        }

        block = block->next;
    }

    return NULL; // heap exhausted — caller must check for this
}

// Merges block b into block a, removing b from the chain
static void coalesce(struct block_header *a, struct block_header *b)
{
    a->size += sizeof(struct block_header) + b->size;
    a->next = b->next;

    if (b->next != NULL)
    {
        b->next->prev = a;
    }
}

// Marks a block free and merges it with any free neighbors
void kfree(void *ptr)
{
    if (ptr == NULL)
    {
        return; // freeing NULL is a no-op, not an error
    }

    struct block_header *block = (struct block_header *)ptr - 1;
    block->free = 1;

    if (block->next != NULL && block->next->free)
    {
        coalesce(block, block->next);
    }

    if (block->prev != NULL && block->prev->free)
    {
        coalesce(block->prev, block);
    }
}