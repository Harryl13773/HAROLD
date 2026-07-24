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
#include "ata.h"
#include "fat.h"
#include "tss.h"
#include "syscall.h"
#include "usermode.h"

extern uint32_t kernel_end;

// Prints A five times, sleeping between each, then returns (task_exit runs automatically)
static void task_a(void)
{
    for (int i = 0; i < 5; i++)
    {
        terminal_writestring("A");
        task_sleep(200);
    }
}

// Prints B forever, sleeping between each
static void task_b(void)
{
    while (1)
    {
        terminal_writestring("B");
        task_sleep(500);
    }
}

// Runs entirely at ring 3, using only int 0x80 — no direct kernel calls
static void usermode_test(void)
{
    const char *msg = "Hello from ring 3!\n";
    for (int i = 0; msg[i] != '\0'; i++)
    {
        __asm__ volatile("int $0x80" : : "a"(0), "b"(msg[i]));
    }

    __asm__ volatile("int $0x80" : : "a"(1)); // exit
}

// Ring-0 trampoline into ring 3 — only one such task may exist at a time, see tss.c
static void usermode_task(void)
{
    usermode_enter(usermode_test);
    terminal_writestring("usermode_task: failed to enter ring 3\n"); // only reached on kmalloc failure
}

void kernel_main(uint32_t multiboot_addr)
{
    // CPU tables and exception/IRQ plumbing
    gdt_install();
    tss_install();
    idt_install();
    isr_install();
    syscall_install();
    pic_remap();
    irq_install();
    keyboard_install();
    pit_init(100);

    // Screen ready before anything writes to it
    terminal_initialize();

    // Detect the disk, prove we can read from it, then mount the filesystem on it
    ata_init();

    uint8_t sector0[ATA_SECTOR_SIZE];
    if (ata_read_sector(0, sector0) == 0)
    {
        if (sector0[510] == 0x55 && sector0[511] == 0xAA)
        {
            terminal_writestring("ATA: sector 0 read OK, boot signature present\n");
        }
        else
        {
            terminal_writestring("ATA: sector 0 read OK, no boot signature (blank disk)\n");
        }
    }

    fat_init();

    // Memory management: frames -> paging -> heap
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

    // Now that the heap exists, try actually reading a real file off the FAT volume
    uint8_t *file_buffer = (uint8_t *)kmalloc(4096);
    if (file_buffer != NULL)
    {
        int bytes_read = fat_read_file("readme.txt", file_buffer, 4096);
        if (bytes_read >= 0)
        {
            terminal_writestring("FAT: read readme.txt (");
            terminal_print_dec((uint32_t)bytes_read);
            terminal_writestring(" bytes):\n");

            for (int i = 0; i < bytes_read; i++)
            {
                terminal_putchar((char)file_buffer[i]);
            }
            terminal_writestring("\n");
        }
        kfree(file_buffer);
    }

    // Tasking depends on the heap, so it comes after
    tasking_init();
    task_create(task_a);
    task_create(task_b);
    task_create(usermode_task);

    // Only now is every subsystem ready for interrupts to actually fire
    __asm__ volatile("sti");

    while (1)
    {
        task_reap(); // frees stack memory for any tasks that have exited
        __asm__ volatile("hlt");
    }
}