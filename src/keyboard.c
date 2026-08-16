// PS/2 keyboard driver: scancode-to-ASCII translation, shift tracking, and a circular input buffer.

#include <stdint.h>
#include "io.h"
#include "irq.h"
#include "terminal.h"
#include "keyboard.h"

// Unshifted scancode -> ASCII (Set 1, US QWERTY)
static const char scancode_ascii[128] =
    {
        0,
        27,
        '1',
        '2',
        '3',
        '4',
        '5',
        '6',
        '7',
        '8',
        '9',
        '0',
        '-',
        '=',
        '\b',
        '\t',
        'q',
        'w',
        'e',
        'r',
        't',
        'y',
        'u',
        'i',
        'o',
        'p',
        '[',
        ']',
        '\n',
        0,
        'a',
        's',
        'd',
        'f',
        'g',
        'h',
        'j',
        'k',
        'l',
        ';',
        '\'',
        '`',
        0,
        '\\',
        'z',
        'x',
        'c',
        'v',
        'b',
        'n',
        'm',
        ',',
        '.',
        '/',
        0,
        '*',
        0,
        ' ',
};

// Shifted scancode -> ASCII, same layout, uppercase letters + symbol row
static const char scancode_ascii_shift[128] =
    {
        0,
        27,
        '!',
        '@',
        '#',
        '$',
        '%',
        '^',
        '&',
        '*',
        '(',
        ')',
        '_',
        '+',
        '\b',
        '\t',
        'Q',
        'W',
        'E',
        'R',
        'T',
        'Y',
        'U',
        'I',
        'O',
        'P',
        '{',
        '}',
        '\n',
        0,
        'A',
        'S',
        'D',
        'F',
        'G',
        'H',
        'J',
        'K',
        'L',
        ':',
        '"',
        '~',
        0,
        '|',
        'Z',
        'X',
        'C',
        'V',
        'B',
        'N',
        'M',
        '<',
        '>',
        '?',
        0,
        '*',
        0,
        ' ',
};

#define SCANCODE_LSHIFT 0x2A
#define SCANCODE_RSHIFT 0x36

// Tracks whether either shift key is currently held
static int shift_held = 0;

#define KB_BUFFER_SIZE 256
static char kb_buffer[KB_BUFFER_SIZE];
static volatile int kb_head = 0;
static volatile int kb_tail = 0;

// Queues a character for keyboard_read_char; drops it if the buffer is full
static void kb_buffer_push(char c)
{
    int next = (kb_head + 1) % KB_BUFFER_SIZE;
    if (next != kb_tail)
    {
        kb_buffer[kb_head] = c;
        kb_head = next;
    }
}

// Fires on every IRQ1: decodes the scancode, tracks shift state, echoes and buffers the character
static void keyboard_handler(void)
{
    uint8_t scancode = inb(0x60);
    int is_release = scancode & 0x80;
    uint8_t code = scancode & 0x7F; // strip the release bit to get the base key

    // Shift keys update state but never print a character themselves
    if (code == SCANCODE_LSHIFT || code == SCANCODE_RSHIFT)
    {
        shift_held = !is_release;
        return;
    }

    if (is_release)
    {
        return; // ignore all other key releases
    }

    char c = shift_held ? scancode_ascii_shift[code] : scancode_ascii[code];
    if (c != 0)
    {
        terminal_putchar(c);
        kb_buffer_push(c);
    }
}

// Enables the keyboard IRQ handler and clears its input buffer
void keyboard_install(void)
{
    irq_set_handler(1, keyboard_handler);
}

// True if a key is waiting in the buffer without blocking
int keyboard_has_char(void)
{
    return kb_head != kb_tail;
}

// Blocks (via hlt, not a busy-spin) until a character is available
char keyboard_read_char(void)
{
    __asm__ volatile("sti"); // this may run with IF=0 if called from inside a syscall
    while (kb_head == kb_tail)
    {
        __asm__ volatile("hlt");
    }

    char c = kb_buffer[kb_tail];
    kb_tail = (kb_tail + 1) % KB_BUFFER_SIZE;
    return c;
}