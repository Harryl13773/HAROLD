// Merges keyboard and serial into a single interactive input source

#include "keyboard.h"
#include "serial.h"
#include "console.h"

char console_read_char(void)
{
    __asm__ volatile("sti"); // this may run with IF=0 if called from inside a syscall

    while (!keyboard_has_char() && !serial_has_char())
    {
        __asm__ volatile("hlt");
    }

    return keyboard_has_char() ? keyboard_read_char() : serial_read_char();
}
