#include <stdint.h>
#include <stddef.h>
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

void terminal_putchar(char c)
{
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
}

void terminal_writestring(const char *str)
{
    for (size_t i = 0; str[i] != '\0'; i++)
    {
        terminal_putchar(str[i]);
    }
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