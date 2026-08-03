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

int listdir(int index, char *name_buf, unsigned int buf_size)
{
    int result;
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(5), "b"(index), "c"(name_buf), "d"(buf_size));
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