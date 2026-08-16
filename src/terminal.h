// Public interface for the VGA text-mode terminal driver.

#ifndef TERMINAL_H
#define TERMINAL_H

#include <stdint.h>

// Clears the screen and resets the cursor to (0,0)
void terminal_initialize(void);

// Handles one character: printable, '\n', or '\b', with wrap + scroll
void terminal_putchar(char c);

// Writes a full string via terminal_putchar
void terminal_writestring(const char *str);

// Prints a 32-bit value as hex, e.g. 0x0100000
void terminal_print_hex(uint32_t value);

// Prints a 32-bit value as decimal
void terminal_print_dec(uint32_t value);

// Draws positioned VGA text with a raw attribute, clipping at screen edges
void terminal_draw_text(int row, int col, const char *text, int len, uint8_t attr);

#endif