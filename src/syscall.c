#include <stdint.h>
#include "idt.h"
#include "terminal.h"
#include "task.h"
#include "syscall.h"

extern void syscall_stub(void);

// Same layout isr.c/irq.c use — matches what syscall_stub actually pushes
struct registers
{
    uint32_t ds;
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags, useresp, ss;
};

#define SYSCALL_WRITE_CHAR 0
#define SYSCALL_EXIT 1

void syscall_handler(struct registers *regs)
{
    switch (regs->eax)
    {
    case SYSCALL_WRITE_CHAR:
        terminal_putchar((char)regs->ebx);
        break;

    case SYSCALL_EXIT:
        task_exit(); // never returns — removes this task and switches away
        break;

    default:
        terminal_writestring("syscall: unknown call number\n");
        break;
    }
}

void syscall_install(void)
{
    // 0xEE = present, DPL 3, interrupt gate — DPL 3 lets ring 3 code invoke this
    idt_set_gate(0x80, (uint32_t)syscall_stub, 0x08, 0xEE);
}