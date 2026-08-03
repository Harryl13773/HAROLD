// ELF32 loader: parses a static executable, maps its segments into a fresh address space, and enters ring 3

#include <stdint.h>
#include "fat.h"
#include "heap.h"
#include "terminal.h"
#include "usermode.h"
#include "paging.h"
#include "pmm.h"
#include "task.h"
#include "elf.h"

// ELF32 file header — byte-for-byte per the spec, 52 bytes
struct elf32_header
{
    uint8_t e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed));

// One program header — describes one segment to load, 32 bytes
struct elf32_program_header
{
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
} __attribute__((packed));

#define ELFCLASS32 1
#define ELFDATA2LSB 1
#define ET_EXEC 2
#define EM_386 3
#define PT_LOAD 1

#define ELF_MAX_FILE_SIZE 65536

int elf_load_and_run(const char *filename)
{
    uint8_t *file_buf = (uint8_t *)kmalloc(ELF_MAX_FILE_SIZE);
    if (file_buf == NULL)
    {
        terminal_writestring("ELF: out of memory reading file\n");
        return -1;
    }

    int file_size = fat_read_file(filename, file_buf, ELF_MAX_FILE_SIZE);
    if (file_size < 0)
    {
        kfree(file_buf);
        return -1; // fat_read_file already printed why
    }

    if ((uint32_t)file_size < sizeof(struct elf32_header))
    {
        terminal_writestring("ELF: file too small to be valid\n");
        kfree(file_buf);
        return -1;
    }

    struct elf32_header *ehdr = (struct elf32_header *)file_buf;

    if (ehdr->e_ident[0] != 0x7F || ehdr->e_ident[1] != 'E' ||
        ehdr->e_ident[2] != 'L' || ehdr->e_ident[3] != 'F')
    {
        terminal_writestring("ELF: bad magic, not an ELF file\n");
        kfree(file_buf);
        return -1;
    }

    if (ehdr->e_ident[4] != ELFCLASS32 || ehdr->e_ident[5] != ELFDATA2LSB)
    {
        terminal_writestring("ELF: not a 32-bit little-endian binary\n");
        kfree(file_buf);
        return -1;
    }

    if (ehdr->e_type != ET_EXEC || ehdr->e_machine != EM_386)
    {
        terminal_writestring("ELF: not a static x86 executable\n");
        kfree(file_buf);
        return -1;
    }

    terminal_writestring("ELF: loading ");
    terminal_writestring(filename);
    terminal_writestring(", entry=");
    terminal_print_hex(ehdr->e_entry);
    terminal_writestring("\n");

    struct elf32_program_header *phdrs = (struct elf32_program_header *)(file_buf + ehdr->e_phoff);
    int segments_loaded = 0;

    for (int i = 0; i < ehdr->e_phnum; i++)
    {
        struct elf32_program_header *ph = &phdrs[i];

        if (ph->p_type != PT_LOAD)
        {
            continue;
        }

        // Each process gets its own private page table for this segment's 4MB region, pre-populated
        // with the kernel's own mappings (so kernel/heap addresses stay correctly accessible), with
        // fresh, genuinely private physical frames mapped in for the segment's own address range —
        // this is what makes two processes at the same virtual address (0x300000 for all of them)
        // actually backed by different physical memory, rather than secretly sharing it
        uint32_t *dir = task_get_current_page_directory();
        uint32_t *table = paging_get_or_create_user_table(dir, ph->p_vaddr);
        if (table == NULL)
        {
            terminal_writestring("ELF: could not set up private memory for this process\n");
            kfree(file_buf);
            return -1;
        }

        uint32_t start_page = ph->p_vaddr & ~(PAGE_SIZE - 1);
        uint32_t end_addr = ph->p_vaddr + ph->p_memsz;
        uint32_t end_page = (end_addr + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

        terminal_writestring("ELF: segment vaddr=");
        terminal_print_hex(ph->p_vaddr);
        terminal_writestring(" memsz=");
        terminal_print_hex(ph->p_memsz);
        terminal_writestring(" pages=");
        terminal_print_dec((end_page - start_page) / PAGE_SIZE);
        terminal_writestring("\n");

        for (uint32_t page_addr = start_page; page_addr < end_page; page_addr += PAGE_SIZE)
        {
            uint32_t frame = pmm_alloc_frame();
            if (frame == 0)
            {
                terminal_writestring("ELF: out of memory mapping process pages\n");
                kfree(file_buf);
                return -1;
            }

            // pmm_free_frame only marks a frame available again — it never clears its contents,
            // so a reused frame would otherwise still carry whatever the previous process left
            // there. Zeroing here, before this process ever touches it, is what actually prevents
            // one process's data from leaking into another's through a recycled physical frame.
            uint8_t *frame_ptr = (uint8_t *)frame;
            for (uint32_t z = 0; z < PAGE_SIZE; z++)
            {
                frame_ptr[z] = 0;
            }

            paging_map_user_page(table, page_addr, frame);
        }

        // The mappings above only take effect once the CPU's TLB is flushed — reloading CR3 does
        // that as a side effect. Without this, the segment copy below could still hit stale,
        // cached translations from before this task's private table existed.
        paging_switch_directory(dir);

        uint8_t *dest = (uint8_t *)ph->p_vaddr;
        uint8_t *src = file_buf + ph->p_offset;

        for (uint32_t b = 0; b < ph->p_filesz; b++)
        {
            dest[b] = src[b];
        }

        for (uint32_t b = ph->p_filesz; b < ph->p_memsz; b++)
        {
            dest[b] = 0; // .bss — present in memory, not in the file
        }

        segments_loaded++;
    }

    if (segments_loaded == 0)
    {
        terminal_writestring("ELF: no loadable segments found\n");
        kfree(file_buf);
        return -1;
    }

    void (*entry)(void) = (void (*)(void))ehdr->e_entry;
    kfree(file_buf); // segments are already copied to their destinations

    usermode_enter(entry);

    terminal_writestring("ELF: usermode_enter returned unexpectedly\n");
    return -1;
}