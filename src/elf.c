#include <stdint.h>
#include "fat.h"
#include "heap.h"
#include "terminal.h"
#include "usermode.h"
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

        // No bounds/collision checking against the kernel or heap here —
        // safe only because the test program's link address is chosen to
        // sit in known-free space. Real isolation needs per-process paging.
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