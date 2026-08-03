// Multiboot info/memory-map structure layouts and the parser's public interface

#ifndef MULTIBOOT_H
#define MULTIBOOT_H

#include <stdint.h>

// Layout of the structure GRUB passes us, matching the Multiboot spec — only the fields we actually need so far
struct multiboot_info
{
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length;
    uint32_t mmap_addr;
} __attribute__((packed));

// One entry in the memory map GRUB builds for us
struct mmap_entry
{
    uint32_t size; // size of this entry, not counting this field itself
    uint64_t addr; // base address of this memory region
    uint64_t len;  // length of this memory region, in bytes
    uint32_t type; // 1 = usable RAM, anything else = reserved/unusable
} __attribute__((packed));

// Walks the memory map and prints each region to the terminal
void multiboot_parse(uint32_t multiboot_addr);

#endif