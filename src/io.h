#ifndef IO_H
#define IO_H

#include <stdint.h>

// Reads a byte from the given I/O port
static inline uint8_t inb(uint16_t port)
{
    uint8_t result;
    __asm__ volatile("inb %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

// Writes a byte to the given I/O port
static inline void outb(uint16_t port, uint8_t data)
{
    __asm__ volatile("outb %0, %1" : : "a"(data), "Nd"(port));
}

// Reads a 16-bit word from the given I/O port
static inline uint16_t inw(uint16_t port)
{
    uint16_t result;
    __asm__ volatile("inw %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

// Writes a 16-bit word to the given I/O port
static inline void outw(uint16_t port, uint16_t data)
{
    __asm__ volatile("outw %0, %1" : : "a"(data), "Nd"(port));
}

// Reads a 32-bit dword from the given I/O port
static inline uint32_t inl(uint16_t port)
{
    uint32_t result;
    __asm__ volatile("inl %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

// Writes a 32-bit dword to the given I/O port
static inline void outl(uint16_t port, uint32_t data)
{
    __asm__ volatile("outl %0, %1" : : "a"(data), "Nd"(port));
}

// Disables interrupts and returns the prior EFLAGS, for protecting a critical section
static inline uint32_t save_and_disable_interrupts(void)
{
    uint32_t flags;
    __asm__ volatile("pushfl\n\tpopl %0\n\tcli" : "=r"(flags) : : "memory");
    return flags;
}

// Restores EFLAGS from save_and_disable_interrupts — safe to nest, unlike a bare sti
static inline void restore_interrupts(uint32_t flags)
{
    __asm__ volatile("pushl %0\n\tpopfl" : : "r"(flags) : "memory", "cc");
}

#endif