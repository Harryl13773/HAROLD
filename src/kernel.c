// Kernel entry point: brings up every subsystem in order and starts the shell and network tasks

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
#include "shell.h"
#include "pci.h"
#include "rtl8139.h"
#include "arp.h"
#include "ip.h"
#include "net.h"

extern uint32_t kernel_end;

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

    // Find what hardware actually exists before touching any of it — the NIC will show up here
    pci_scan();

    // Now that we know the NIC exists on PCI, bring it up and read its real MAC address
    rtl8139_init();

    if (rtl8139_is_present())
    {
        // 192.168.18.1 is your Mac's own address on the vmnet-host bridge — a real device that will
        // actually answer ARP, unlike 10.0.2.2 which only existed under the old SLIRP-based setup.
        // Resolved twice on purpose: the first call is a real request/reply, the second should hit
        // the cache instead — "ARP: cache hit, no request sent" is what confirms that actually happened.
        uint8_t bridge_ip[4] = {192, 168, 18, 1};
        uint8_t bridge_mac[6];
        arp_resolve(bridge_ip, bridge_mac);
        arp_resolve(bridge_ip, bridge_mac);

        // 192.168.18.50 matches the vmnet-host subnet above — update this if you switch back to
        // SLIRP (`run`/`run-iso`, 10.0.2.15) or to vmnet-shared (whatever subnet it assigns)
        uint8_t our_ip[4] = {192, 168, 18, 50};
        ip_set_address(our_ip);
    }

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
    // Reserving through kernel_end + HEAP_SIZE, not just kernel_end, since the heap that heap_init()
    // sets up below occupies that whole range — pmm_alloc_frame() is about to be used for real
    // (per-process page allocation), so handing out a frame the heap already owns is a live risk now,
    // not just a theoretical one
    struct multiboot_info *mb_info = (struct multiboot_info *)multiboot_addr;
    pmm_init(mb_info, (uint32_t)&kernel_end + HEAP_SIZE);

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
    task_create(shell_task);

    if (rtl8139_is_present())
    {
        task_create(net_task); // keeps answering ARP/ping in the background, independent of the shell
    }

    // Only now is every subsystem ready for interrupts to actually fire
    __asm__ volatile("sti");

    while (1)
    {
        task_reap(); // frees stack memory for any tasks that have exited
        __asm__ volatile("hlt");
    }
}