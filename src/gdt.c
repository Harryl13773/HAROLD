/*
building the actual GDT data structure in memory and hands to the CPU
*/

#include <stdint.h>
#include "gdt.h"

// describing the byte layout
struct gdt_entry
{
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_middle;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
} __attribute__((packed));

// describing the small pointer structure the lgdt instruction actually consumes
struct gdt_ptr
{
    uint16_t limit; // total size of the GDT
    uint32_t base;  // where the memory address where the table starts
} __attribute__((packed));

static struct gdt_entry gdt[3]; // array of 3 entries (null, code, data)
static struct gdt_ptr gp;       // describe the gdt to the CPU

extern void gdt_flush(uint32_t);

/**
 * base and limit splits across the struct's fragmented tables
 */
static void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran)
{
    gdt[num].base_low = (base & 0xFFFF);
    gdt[num].base_middle = (base >> 16) & 0xFF;
    gdt[num].base_high = (base >> 24) & 0xFF;

    gdt[num].limit_low = (limit & 0xFFFF);
    gdt[num].granularity = (limit >> 16) & 0x0F;
    gdt[num].granularity |= gran & 0xF0;

    gdt[num].access = access;
}

// call gdt_flush, passing along the address of gp and send into the CPU
void gdt_install(void)
{
    gp.limit = (sizeof(struct gdt_entry) * 3) - 1;
    gp.base = (uint32_t)&gdt;

    gdt_set_gate(0, 0, 0, 0, 0);                // null descriptor
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF); // code segment
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF); // data segment

    gdt_flush((uint32_t)&gp);
}
