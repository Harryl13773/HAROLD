#include <stdint.h>
#include <stddef.h>
#include "gdt.h"
#include "idt.h"
#include "isr.h"
#include "pic.h"
#include "irq.h"
#include "keyboard.h"
#include "terminal.h"
#include "multiboot.h"
#include "pmm.h"
#include "paging.h"
#include "heap.h"
#include "pit.h"
#include "task.h"

extern uint32_t kernel_end;

static void test_task(void)
{
    terminal_writestring("Task 1 is running!\n");
    for (;;)
    {
        __asm__ volatile("hlt");
    }
}

static void task_a(void)
{
    while (1)
    {
        terminal_writestring("A");
        __asm__ volatile("hlt");
    }
}

static void task_b(void)
{
    while (1)
    {
        terminal_writestring("B");
        __asm__ volatile("hlt");
    }
}

void kernel_main(uint32_t multiboot_addr)
{
    // CPU tables and exception/IRQ plumbing
    gdt_install();
    idt_install();
    isr_install();
    pic_remap();
    irq_install();
    keyboard_install();
    pit_init(100);

    // Screen ready before anything writes to it
    terminal_initialize();

    // Memory management, in dependency order: frames -> paging -> heap
    struct multiboot_info *mb_info = (struct multiboot_info *)multiboot_addr;
    pmm_init(mb_info, (uint32_t)&kernel_end);

    terminal_writestring("Hello from my operating system!\n");
    multiboot_parse(multiboot_addr);

    terminal_writestring("Free frames: ");
    terminal_print_dec(pmm_get_free_frame_count());
    terminal_writestring(" / ");
    terminal_print_dec(pmm_get_total_frame_count());
    terminal_writestring("\n");

    paging_init();
    heap_init();

    // Tasking depends on the heap, so it comes after
    tasking_init();
    task_create(task_a);
    task_create(task_b);

    int id = task_create(test_task);
    if (id > 0)
    {
        task_switch_to(id);
    }

    // Only now is every subsystem ready for interrupts to actually fire
    __asm__ volatile("sti");

    while (1)
    {
        __asm__ volatile("hlt");
    }
}