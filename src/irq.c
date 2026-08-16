// IRQ dispatch: remapped PIC interrupt stubs, PIC EOI acknowledgment, and handler registration.

#include <stdint.h>
#include "idt.h"
#include "io.h"
#include "irq.h"

extern void irq0(void);
extern void irq1(void);
extern void irq2(void);
extern void irq3(void);
extern void irq4(void);
extern void irq5(void);
extern void irq6(void);
extern void irq7(void);
extern void irq8(void);
extern void irq9(void);
extern void irq10(void);
extern void irq11(void);
extern void irq12(void);
extern void irq13(void);
extern void irq14(void);
extern void irq15(void);

// Matches what irq_common_stub pushed onto the stack
struct registers
{
    uint32_t ds;
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags, useresp, ss;
};

// One slot per IRQ line, so drivers can register without editing this file
static irq_handler_t irq_routines[16] = {0};

// Lets a driver (keyboard, timer, etc.) attach itself to a specific IRQ line
void irq_set_handler(int irq, irq_handler_t handler)
{
    irq_routines[irq] = handler;
}

// Called by every IRQ stub — acknowledges the PIC immediately, then dispatches
void irq_handler(struct registers *regs)
{
    int irq = regs->int_no - 32;

    // Sent first — a handler that triggers a task switch may never return to this point
    if (irq >= 8)
    {
        outb(0xA0, 0x20);
    }
    outb(0x20, 0x20);

    if (irq_routines[irq] != 0)
    {
        irq_routines[irq]();
    }
}

// Registers all 16 IRQ stubs into the IDT
void irq_install(void)
{
    idt_set_gate(32, (uint32_t)irq0, 0x08, 0x8E);
    idt_set_gate(33, (uint32_t)irq1, 0x08, 0x8E);
    idt_set_gate(34, (uint32_t)irq2, 0x08, 0x8E);
    idt_set_gate(35, (uint32_t)irq3, 0x08, 0x8E);
    idt_set_gate(36, (uint32_t)irq4, 0x08, 0x8E);
    idt_set_gate(37, (uint32_t)irq5, 0x08, 0x8E);
    idt_set_gate(38, (uint32_t)irq6, 0x08, 0x8E);
    idt_set_gate(39, (uint32_t)irq7, 0x08, 0x8E);
    idt_set_gate(40, (uint32_t)irq8, 0x08, 0x8E);
    idt_set_gate(41, (uint32_t)irq9, 0x08, 0x8E);
    idt_set_gate(42, (uint32_t)irq10, 0x08, 0x8E);
    idt_set_gate(43, (uint32_t)irq11, 0x08, 0x8E);
    idt_set_gate(44, (uint32_t)irq12, 0x08, 0x8E);
    idt_set_gate(45, (uint32_t)irq13, 0x08, 0x8E);
    idt_set_gate(46, (uint32_t)irq14, 0x08, 0x8E);
    idt_set_gate(47, (uint32_t)irq15, 0x08, 0x8E);
}