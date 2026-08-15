ENTRY(_start)

/* Two separate LOAD segments instead of one combined RWX one — elf.c already loops over every
   PT_LOAD entry (it has to, for programs with just one), so this needs no loader changes. */
PHDRS
{
    text PT_LOAD FLAGS(5); /* R+X */
    data PT_LOAD FLAGS(6); /* R+W */
}

SECTIONS
{
    . = 0x300000;

    .text   : { *(.text) } :text
    .rodata : { *(.rodata) } :text

    . = ALIGN(0x1000);

    .data   : { *(.data) } :data
    .bss    : { *(COMMON) *(.bss) } :data
}