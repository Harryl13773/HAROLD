// VGA text-mode terminal driver: character output, cursor tracking, scrolling, and hex/decimal printing.

#include <stdint.h>
#include <stddef.h>
#include "io.h"
#include "serial.h"
#include "terminal.h"

#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_COLOR 0x0F00

static volatile uint16_t *video_memory = (uint16_t *)0xB8000;
static int cursor_row = 0;
static int cursor_col = 0;

// Writes one glyph at a specific cell, independent of the live cursor
static void terminal_put_entry_at(char c, int col, int row)
{
    video_memory[row * VGA_WIDTH + col] = (uint16_t)c | VGA_COLOR;
}

// Moves every row up by one, clears the bottom row
static void terminal_scroll(void)
{
    for (int row = 1; row < VGA_HEIGHT; row++)
    {
        for (int col = 0; col < VGA_WIDTH; col++)
        {
            video_memory[(row - 1) * VGA_WIDTH + col] = video_memory[row * VGA_WIDTH + col];
        }
    }

    for (int col = 0; col < VGA_WIDTH; col++)
    {
        terminal_put_entry_at(' ', col, VGA_HEIGHT - 1);
    }

    cursor_row = VGA_HEIGHT - 1;
}

// Clears the screen and resets the cursor to (0,0)
void terminal_initialize(void)
{
    for (int row = 0; row < VGA_HEIGHT; row++)
    {
        for (int col = 0; col < VGA_WIDTH; col++)
        {
            terminal_put_entry_at(' ', col, row);
        }
    }

    cursor_row = 0;
    cursor_col = 0;
}

// Handles one character: printable, '\n', or '\b', with wrap + scroll
void terminal_putchar(char c)
{

    // Protect shared video and cursor state from concurrent writes
    uint32_t flags = save_and_disable_interrupts();

    // Mirror output to serial so scrolled VGA text remains logged in order
    serial_putchar(c);

    if (c == '\n')
    {
        cursor_col = 0;
        cursor_row++;
    }
    else if (c == '\b')
    {
        if (cursor_col > 0)
        {
            cursor_col--;
        }
        else if (cursor_row > 0)
        {
            cursor_row--;
            cursor_col = VGA_WIDTH - 1;
        }

        terminal_put_entry_at(' ', cursor_col, cursor_row);
    }
    else
    {
        terminal_put_entry_at(c, cursor_col, cursor_row);
        cursor_col++;
    }

    if (cursor_col >= VGA_WIDTH)
    {
        cursor_col = 0;
        cursor_row++;
    }

    if (cursor_row >= VGA_HEIGHT)
    {
        terminal_scroll();
    }

    restore_interrupts(flags);
}

// Writes a full string via terminal_putchar
void terminal_writestring(const char *str)
{

    // Protect the whole string to prevent output from different tasks interleaving
    uint32_t flags = save_and_disable_interrupts();

    for (size_t i = 0; str[i] != '\0'; i++)
    {
        terminal_putchar(str[i]);
    }

    restore_interrupts(flags);
}

// Prints a 32-bit value as 8 hex digits
void terminal_print_hex(uint32_t value)
{
    char hex_chars[] = "0123456789ABCDEF";
    terminal_writestring("0x");

    for (int i = 28; i >= 0; i -= 4)
    {
        terminal_putchar(hex_chars[(value >> i) & 0xF]);
    }
}

// Prints a 32-bit value as decimal, most significant digit first
void terminal_print_dec(uint32_t value)
{
    if (value == 0)
    {
        terminal_putchar('0');
        return;
    }

    char buffer[10]; // max digits in a 32-bit unsigned value
    int i = 0;

    while (value > 0)
    {
        buffer[i++] = '0' + (value % 10);
        value /= 10;
    }

    while (i > 0)
    {
        terminal_putchar(buffer[--i]);
    }
}

// Draws positioned VGA text with a raw attribute, clipping at screen edges
void terminal_draw_text(int row, int col, const char *text, int len, uint8_t attr)
{
    if (row < 0 || row >= VGA_HEIGHT)
    {
        return;
    }

    // Shared global VGA state, same reasoning as terminal_putchar's own locking
    uint32_t flags = save_and_disable_interrupts();

    for (int i = 0; i < len; i++)
    {
        int c = col + i;
        if (c < 0 || c >= VGA_WIDTH)
        {
            continue; // clip rather than wrap into the next row or overrun the buffer
        }
        video_memory[row * VGA_WIDTH + c] = (uint16_t)(uint8_t)text[i] | ((uint16_t)attr << 8);
    }

    restore_interrupts(flags);
}