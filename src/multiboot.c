// Parses the GRUB-provided Multiboot info structure and prints the usable memory map.

#include <stdint.h>
#include "multiboot.h"
#include "terminal.h"

// Walks the memory map and prints each usable region
void multiboot_parse(uint32_t multiboot_addr)
{
    struct multiboot_info *info = (struct multiboot_info *)multiboot_addr;

    // Bit 6 confirms mmap_addr/mmap_length are valid
    if (!(info->flags & (1 << 6)))
    {
        terminal_writestring("No memory map provided by bootloader\n");
        return;
    }

    struct mmap_entry *entry = (struct mmap_entry *)info->mmap_addr;
    uint32_t end = info->mmap_addr + info->mmap_length;

    while ((uint32_t)entry < end)
    {
        if (entry->type == 1)
        {
            terminal_writestring("Usable RAM: base=");
            terminal_print_hex((uint32_t)entry->addr);
            terminal_writestring(" length=");
            terminal_print_hex((uint32_t)entry->len);
            terminal_writestring("\n");
        }

        entry = (struct mmap_entry *)((uint32_t)entry + entry->size + sizeof(uint32_t));
    }
}