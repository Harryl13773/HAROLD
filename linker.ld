/* telling the linker that this is where it starts */
ENTRY(_start)

SECTIONS
{

    /* starting at address 1MB */
    . = 1M;


    /* output section: text data */
    .text BLOCK(4K) : ALIGN(4K)
    {
        *(.multiboot)
        *(.text)
    }

    /* output section: read-only data */
    .rodata BLOCK(4K) : ALIGN(4K)
    {
        *(.rodata)
    }

    /* output section: data */
    .data BLOCK(4K) : ALIGN(4K)
    {
        *(.data)
    }

    /* output section: bss */
    .bss BLOCK(4K) : ALIGN(4K)
    {
        *(COMMON)
        *(.bss)
    }
}