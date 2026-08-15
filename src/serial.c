// COM1 serial driver: a persistent log outside the 25-row VGA buffer, so scrolled-off history
// (including exactly when a fault happened relative to everything else) is never actually lost —
// QEMU's -serial flag captures it to a file/stdio even in a headless run

#include <stdint.h>
#include "io.h"
#include "serial.h"

#define COM1 0x3F8

void serial_init(void)
{
    outb(COM1 + 1, 0x00); // disable UART interrupts — we poll
    outb(COM1 + 3, 0x80); // enable DLAB to set the baud rate divisor
    outb(COM1 + 0, 0x03); // divisor low byte: 38400 baud (115200 / 3)
    outb(COM1 + 1, 0x00); // divisor high byte
    outb(COM1 + 3, 0x03); // 8 bits, no parity, one stop bit; also clears DLAB
    outb(COM1 + 2, 0xC7); // enable + clear the FIFOs, 14-byte trigger
    outb(COM1 + 4, 0x0B); // RTS/DSR set, IRQs off (OUT2 low)
}

// Bit 5 of the line status register — set when the transmit holding register can accept a byte
static int transmit_empty(void)
{
    return inb(COM1 + 5) & 0x20;
}

static void serial_raw_putchar(char c)
{
    while (!transmit_empty())
    {
    }

    outb(COM1, (uint8_t)c);
}

void serial_putchar(char c)
{
    if (c == '\n')
    {
        serial_raw_putchar('\r'); // real serial terminals need CRLF, not bare LF
    }
    serial_raw_putchar(c);
}

void serial_writestring(const char *str)
{
    for (int i = 0; str[i] != '\0'; i++)
    {
        serial_putchar(str[i]);
    }
}
