// CPU exception (ISR 0-31) handlers: prints a fault banner, then isolates the faulting task or halts the kernel

#include <stdint.h>
#include "idt.h"
#include "isr.h"
#include "task.h"

extern void isr0(void);
extern void isr1(void);
extern void isr2(void);
extern void isr3(void);
extern void isr4(void);
extern void isr5(void);
extern void isr6(void);
extern void isr7(void);
extern void isr8(void);
extern void isr9(void);
extern void isr10(void);
extern void isr11(void);
extern void isr12(void);
extern void isr13(void);
extern void isr14(void);
extern void isr15(void);
extern void isr16(void);
extern void isr17(void);
extern void isr18(void);
extern void isr19(void);
extern void isr20(void);
extern void isr21(void);
extern void isr22(void);
extern void isr23(void);
extern void isr24(void);
extern void isr25(void);
extern void isr26(void);
extern void isr27(void);
extern void isr28(void);
extern void isr29(void);
extern void isr30(void);
extern void isr31(void);

struct registers
{
    uint32_t ds;
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags, useresp, ss;
};

static const char *exception_messages[32] =
    {
        "Division By Zero", "Debug", "Non Maskable Interrupt", "Breakpoint",
        "Into Detected Overflow", "Out of Bounds", "Invalid Opcode", "No Coprocessor",
        "Double Fault", "Coprocessor Segment Overrun", "Bad TSS", "Segment Not Present",
        "Stack Fault", "General Protection Fault", "Page Fault", "Unknown Interrupt",
        "Coprocessor Fault", "Alignment Check", "Machine Check", "Reserved",
        "Reserved", "Reserved", "Reserved", "Reserved",
        "Reserved", "Reserved", "Reserved", "Reserved",
        "Reserved", "Reserved", "Reserved", "Reserved"};

#define VGA_COLS 80

// Blanks an entire row so old scrolled text can't bleed through past what we write
static void clear_row(int row)
{
    volatile uint16_t *video_memory = (uint16_t *)0xB8000;
    int offset = row * VGA_COLS;

    for (int i = 0; i < VGA_COLS; i++)
    {
        video_memory[offset + i] = (uint16_t)' ' | 0x4F00; // white on red
    }
}

// Writes a string directly to VGA memory at a given row
static void print_at(const char *str, int row)
{
    volatile uint16_t *video_memory = (uint16_t *)0xB8000;
    int offset = row * VGA_COLS;

    clear_row(row);

    for (int i = 0; str[i] != '\0'; i++)
    {
        video_memory[offset + i] = (uint16_t)str[i] | 0x4F00; // white on red
    }
}

// Writes an 8-digit hex value directly to VGA memory, same red banner style
static void print_hex_at(uint32_t value, int row)
{
    volatile uint16_t *video_memory = (uint16_t *)0xB8000;
    char hex_chars[] = "0123456789ABCDEF";
    int offset = row * VGA_COLS;

    clear_row(row);

    video_memory[offset] = (uint16_t)'0' | 0x4F00;
    video_memory[offset + 1] = (uint16_t)'x' | 0x4F00;

    for (int i = 0; i < 8; i++)
    {
        video_memory[offset + 2 + i] = (uint16_t)hex_chars[(value >> (28 - i * 4)) & 0xF] | 0x4F00;
    }
}

// Reads CR2 — the CPU stores the faulting address here during a page fault
static inline uint32_t read_cr2(void)
{
    uint32_t value;
    __asm__ volatile("mov %%cr2, %0" : "=r"(value));
    return value;
}

// Called by every exception stub — reports the fault, then isolates or halts depending on CPL
void fault_handler(struct registers *regs)
{
    print_at("EXCEPTION:", 2);
    print_at(exception_messages[regs->int_no], 3);

    if (regs->int_no == 14) // page fault — CR2 tells us exactly what address failed
    {
        print_at("Faulting address:", 4);
        print_hex_at(read_cr2(), 5);
    }

    if ((regs->cs & 0x3) == 3) // CPL 3 — the fault came from a ring 3 task, not the kernel itself
    {
        print_at("Ring 3 fault - terminating task:", 6);
        print_hex_at((uint32_t)task_current_id(), 7);
        task_exit(); // never returns — kills this task and switches to another
    }

    print_at("Kernel-mode fault - halting", 6);

    for (;;)
    {
        __asm__ volatile("hlt");
    }
}

// Points IDT vectors 0-31 at their matching stub
void isr_install(void)
{
    idt_set_gate(0, (uint32_t)isr0, 0x08, 0x8E);
    idt_set_gate(1, (uint32_t)isr1, 0x08, 0x8E);
    idt_set_gate(2, (uint32_t)isr2, 0x08, 0x8E);
    idt_set_gate(3, (uint32_t)isr3, 0x08, 0x8E);
    idt_set_gate(4, (uint32_t)isr4, 0x08, 0x8E);
    idt_set_gate(5, (uint32_t)isr5, 0x08, 0x8E);
    idt_set_gate(6, (uint32_t)isr6, 0x08, 0x8E);
    idt_set_gate(7, (uint32_t)isr7, 0x08, 0x8E);
    idt_set_gate(8, (uint32_t)isr8, 0x08, 0x8E);
    idt_set_gate(9, (uint32_t)isr9, 0x08, 0x8E);
    idt_set_gate(10, (uint32_t)isr10, 0x08, 0x8E);
    idt_set_gate(11, (uint32_t)isr11, 0x08, 0x8E);
    idt_set_gate(12, (uint32_t)isr12, 0x08, 0x8E);
    idt_set_gate(13, (uint32_t)isr13, 0x08, 0x8E);
    idt_set_gate(14, (uint32_t)isr14, 0x08, 0x8E);
    idt_set_gate(15, (uint32_t)isr15, 0x08, 0x8E);
    idt_set_gate(16, (uint32_t)isr16, 0x08, 0x8E);
    idt_set_gate(17, (uint32_t)isr17, 0x08, 0x8E);
    idt_set_gate(18, (uint32_t)isr18, 0x08, 0x8E);
    idt_set_gate(19, (uint32_t)isr19, 0x08, 0x8E);
    idt_set_gate(20, (uint32_t)isr20, 0x08, 0x8E);
    idt_set_gate(21, (uint32_t)isr21, 0x08, 0x8E);
    idt_set_gate(22, (uint32_t)isr22, 0x08, 0x8E);
    idt_set_gate(23, (uint32_t)isr23, 0x08, 0x8E);
    idt_set_gate(24, (uint32_t)isr24, 0x08, 0x8E);
    idt_set_gate(25, (uint32_t)isr25, 0x08, 0x8E);
    idt_set_gate(26, (uint32_t)isr26, 0x08, 0x8E);
    idt_set_gate(27, (uint32_t)isr27, 0x08, 0x8E);
    idt_set_gate(28, (uint32_t)isr28, 0x08, 0x8E);
    idt_set_gate(29, (uint32_t)isr29, 0x08, 0x8E);
    idt_set_gate(30, (uint32_t)isr30, 0x08, 0x8E);
    idt_set_gate(31, (uint32_t)isr31, 0x08, 0x8E);
}