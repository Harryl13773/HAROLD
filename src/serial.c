/*
COM1 serial driver: a persistent log outside the 25-row VGA buffer, so scrolled-off history
(including exactly when a fault happened relative to everything else) is never actually lost,
QEMU's -serial flag captures it to a file/stdio even in a headless run.
*/

#include <stdint.h>
#include "io.h"
#include "irq.h"
#include "pic.h"
#include "terminal.h"
#include "serial.h"

#define COM1 0x3F8

#define SERIAL_RX_BUFFER_SIZE 256
static char serial_rx_buffer[SERIAL_RX_BUFFER_SIZE];
static volatile int serial_rx_head = 0;
static volatile int serial_rx_tail = 0;

// Queues a received byte, dropping it if the serial buffer is full
static void serial_rx_push(char c)
{
    int next = (serial_rx_head + 1) % SERIAL_RX_BUFFER_SIZE;
    if (next != serial_rx_tail)
    {
        serial_rx_buffer[serial_rx_head] = c;
        serial_rx_head = next;
    }
}

// Bit 0 of the line status register — set while a received byte is waiting in the RBR
static int serial_data_ready(void)
{
    return inb(COM1 + 5) & 0x01;
}

// Fires on every IRQ4 — drains and echoes any bytes the UART has received
static void serial_handler(void)
{
    while (serial_data_ready()) // drain the FIFO — more than one byte can be waiting per IRQ
    {
        char c = (char)inb(COM1);
        terminal_putchar(c); // local echo — same "echo then buffer" order as keyboard_handler
        serial_rx_push(c);
    }
}

// Initializes COM1 at 38400 baud, 8N1, with FIFO enabled, and wires up IRQ4-driven receive
void serial_init(void)
{
    outb(COM1 + 1, 0x00); // disable UART interrupts while we finish setup
    outb(COM1 + 3, 0x80); // enable DLAB to set the baud rate divisor
    outb(COM1 + 0, 0x03); // divisor low byte: 38400 baud (115200 / 3)
    outb(COM1 + 1, 0x00); // divisor high byte
    outb(COM1 + 3, 0x03); // 8 bits, no parity, one stop bit; also clears DLAB
    outb(COM1 + 2, 0xC7); // enable + clear the FIFOs, 14-byte trigger
    outb(COM1 + 4, 0x0B); // DTR/RTS set, OUT2 set — OUT2 is what actually lets the UART drive IRQ4

    irq_set_handler(4, serial_handler); // Register COM1's IRQ4 handler before enabling interrupts.
    pic_unmask_irq(4);                  // Explicitly unmask IRQ4.
    outb(COM1 + 1, 0x01);               // Enable receive-data interrupts.
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

// Writes one character, blocking until the transmit holding register is empty
void serial_putchar(char c)
{
    if (c == '\n')
    {
        serial_raw_putchar('\r'); // real serial terminals need CRLF, not bare LF
    }
    serial_raw_putchar(c);
}

// Writes a full string via serial_putchar
void serial_writestring(const char *str)
{
    for (int i = 0; str[i] != '\0'; i++)
    {
        serial_putchar(str[i]);
    }
}

// True if a received byte is waiting to be read without blocking
int serial_has_char(void)
{
    return serial_rx_head != serial_rx_tail;
}

// Blocks (via hlt, not a busy-spin) until a byte arrives on COM1, same pattern as keyboard_read_char
char serial_read_char(void)
{
    __asm__ volatile("sti"); // this may run with IF=0 if called from inside a syscall

    while (serial_rx_head == serial_rx_tail)
    {
        __asm__ volatile("hlt");
    }

    char c = serial_rx_buffer[serial_rx_tail];
    serial_rx_tail = (serial_rx_tail + 1) % SERIAL_RX_BUFFER_SIZE;
    return c;
}
