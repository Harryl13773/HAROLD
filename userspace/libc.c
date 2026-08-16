// Userspace libc: int 0x80 syscall wrappers plus minimal string/memory helpers for ELF programs

#include "libc.h"

int write(const char *buf, unsigned int len)
{
    int result;
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(0), "b"(buf), "c"(len));
    return result;
}

int read(int fd, char *buf, unsigned int len)
{
    int result;
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(2), "b"(fd), "c"(buf), "d"(len));
    return result;
}

int open(const char *filename)
{
    int result;
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(3), "b"(filename));
    return result;
}

int close(int fd)
{
    int result;
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(4), "b"(fd));
    return result;
}

int listdir(const char *dir_path, int index, char *name_buf, unsigned int buf_size)
{
    int result;
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(5), "b"(index), "c"(name_buf), "d"(buf_size), "S"(dir_path));
    return result;
}

int socket_accept(void)
{
    int result;
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(6));
    return result;
}

int socket_recv(int sockfd, char *buf, unsigned int max_len)
{
    int result;
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(7), "b"(sockfd), "c"(buf), "d"(max_len));
    return result;
}

int socket_send(int sockfd, const char *buf, unsigned int len)
{
    int result;
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(8), "b"(sockfd), "c"(buf), "d"(len));
    return result;
}

int socket_close(int sockfd)
{
    int result;
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(9), "b"(sockfd));
    return result;
}

int open_write(const char *filename, int mode)
{
    int result;
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(10), "b"(filename), "c"(mode));
    return result;
}

int fwrite(int fd, const char *buf, unsigned int len)
{
    int result;
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(11), "b"(fd), "c"(buf), "d"(len));
    return result;
}

int mkdir(const char *path)
{
    int result;
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(12), "b"(path));
    return result;
}

int seek(int fd, unsigned int offset)
{
    int result;
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(13), "b"(fd), "c"(offset));
    return result;
}

int dns_resolve(const char *hostname, unsigned char out_ip[4])
{
    int result;
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(14), "b"(hostname), "c"(out_ip));
    return result;
}

void exit(void)
{
    __asm__ volatile("int $0x80" : : "a"(1));
    for (;;)
    {
    } // never reached
}

unsigned int strlen(const char *s)
{
    unsigned int len = 0;
    while (s[len] != '\0')
    {
        len++;
    }
    return len;
}

void *memset(void *dest, int value, unsigned int len)
{
    unsigned char *d = (unsigned char *)dest;
    for (unsigned int i = 0; i < len; i++)
    {
        d[i] = (unsigned char)value;
    }
    return dest;
}

void *memcpy(void *dest, const void *src, unsigned int len)
{
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    for (unsigned int i = 0; i < len; i++)
    {
        d[i] = s[i];
    }
    return dest;
}

int strcmp(const char *a, const char *b)
{
    while (*a != '\0' && *a == *b)
    {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

// A first-fit free list over a fixed static array, same design as the kernel's own heap.c —
// block header stored immediately before its payload, split on alloc, coalesced on free. Unlike
// heap.c, no interrupt-disabling is needed here: this array lives inside the process's own
// already-private segment (see elf.c), so no other task can ever touch it, and a process is
// single-threaded — nothing else here to preempt into a half-updated free list.
#define USER_HEAP_SIZE 65536
#define MALLOC_ALIGNMENT 8
#define MALLOC_MIN_SPLIT 16

struct malloc_block_header
{
    unsigned int size;
    int free_flag;
    struct malloc_block_header *next;
    struct malloc_block_header *prev;
};

static unsigned char user_heap[USER_HEAP_SIZE];
static struct malloc_block_header *malloc_heap_start = 0;

static unsigned int malloc_align_up(unsigned int size)
{
    return (size + (MALLOC_ALIGNMENT - 1)) & ~(MALLOC_ALIGNMENT - 1);
}

// Breaks a block in two if the leftover space is worth keeping as its own free block
static void malloc_split_block(struct malloc_block_header *block, unsigned int size)
{
    if (block->size < size + sizeof(struct malloc_block_header) + MALLOC_MIN_SPLIT)
    {
        return; // leftover too small to be useful — hand over the whole block
    }

    struct malloc_block_header *new_block =
        (struct malloc_block_header *)((unsigned char *)(block + 1) + size);

    new_block->size = block->size - size - sizeof(struct malloc_block_header);
    new_block->free_flag = 1;
    new_block->next = block->next;
    new_block->prev = block;

    if (block->next != 0)
    {
        block->next->prev = new_block;
    }

    block->next = new_block;
    block->size = size;
}

void *malloc(unsigned int size)
{
    if (size == 0)
    {
        return 0;
    }

    if (malloc_heap_start == 0) // lazily set up on first call — no process-startup hook exists to do it earlier
    {
        malloc_heap_start = (struct malloc_block_header *)user_heap;
        malloc_heap_start->size = USER_HEAP_SIZE - sizeof(struct malloc_block_header);
        malloc_heap_start->free_flag = 1;
        malloc_heap_start->next = 0;
        malloc_heap_start->prev = 0;
    }

    size = malloc_align_up(size);
    struct malloc_block_header *block = malloc_heap_start;

    while (block != 0)
    {
        if (block->free_flag && block->size >= size)
        {
            malloc_split_block(block, size);
            block->free_flag = 0;
            return (void *)(block + 1); // payload starts right after the header
        }
        block = block->next;
    }

    return 0; // heap exhausted — caller must check
}

// Merges block b into block a, removing b from the chain
static void malloc_coalesce(struct malloc_block_header *a, struct malloc_block_header *b)
{
    a->size += sizeof(struct malloc_block_header) + b->size;
    a->next = b->next;

    if (b->next != 0)
    {
        b->next->prev = a;
    }
}

void free(void *ptr)
{
    if (ptr == 0)
    {
        return; // freeing NULL is a no-op, not an error
    }

    struct malloc_block_header *block = (struct malloc_block_header *)ptr - 1;
    block->free_flag = 1;

    if (block->next != 0 && block->next->free_flag)
    {
        malloc_coalesce(block, block->next);
    }

    if (block->prev != 0 && block->prev->free_flag)
    {
        malloc_coalesce(block->prev, block);
    }
}

int itoa(int value, char *buf)
{
    if (value == 0)
    {
        buf[0] = '0';
        buf[1] = '\0';
        return 1;
    }

    int negative = value < 0;
    unsigned int uvalue = negative ? (unsigned int)(-value) : (unsigned int)value;

    char tmp[12];
    int i = 0;
    while (uvalue > 0)
    {
        tmp[i++] = '0' + (uvalue % 10);
        uvalue /= 10;
    }

    int len = 0;
    if (negative)
    {
        buf[len++] = '-';
    }
    while (i > 0)
    {
        buf[len++] = tmp[--i];
    }
    buf[len] = '\0';
    return len;
}

int atoi(const char *s)
{
    int i = 0;
    int negative = 0;

    if (s[i] == '-')
    {
        negative = 1;
        i++;
    }

    int value = 0;
    while (s[i] >= '0' && s[i] <= '9')
    {
        value = value * 10 + (s[i] - '0');
        i++;
    }

    return negative ? -value : value;
}