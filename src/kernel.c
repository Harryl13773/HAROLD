#include <stdint.h>
#include <stddef.h> // providing size_t
#include "gdt.h"

void kernel_main(void)
{

    gdt_install();

    // text buffer pointer
    volatile uint16_t *video_memory = (uint16_t *)0xB8000;

    const char *message = "Hello from my operating system!";

    // walks the string until the null terminator, using size_t
    for (size_t i = 0; message[i] != '\0'; i++)
    {

        // background black(0), foreground = white(F)
        video_memory[i] = (uint16_t)message[i] | 0x0F00;
    }

    // halt loop
    while (1)
    {
        __asm__ volatile("hlt");
    }
}
