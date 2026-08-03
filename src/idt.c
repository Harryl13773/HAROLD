// Builds and loads the Interrupt Descriptor Table

#include <stdint.h>
#include "idt.h"

// Layout of a single IDT entry, as expected by the CPU
struct idt_entry
{
    uint16_t base_low;
    uint16_t sel;
    uint8_t always0;
    uint8_t flags;
    uint16_t base_high;
} __attribute__((packed));

// Structure passed to lidt: table size + location
struct idt_ptr
{
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

// The actual 256-entry interrupt table
static struct idt_entry idt[256];
static struct idt_ptr ip;

// Assembly routine that executes lidt
extern void idt_load(uint32_t);

// Fills in one IDT entry
void idt_set_gate(int num, uint32_t base, uint16_t sel, uint8_t flags)
{
    idt[num].base_low = base & 0xFFFF;
    idt[num].base_high = (base >> 16) & 0xFFFF;

    idt[num].sel = sel;
    idt[num].always0 = 0;
    idt[num].flags = flags;
}

// Zeroes out all 256 entries and loads the IDT into the CPU
void idt_install(void)
{
    ip.limit = (sizeof(struct idt_entry) * 256) - 1;
    ip.base = (uint32_t)&idt;

    for (int i = 0; i < 256; i++)
    {
        idt_set_gate(i, 0, 0, 0);
    }

    idt_load((uint32_t)&ip);
}