// PS/2 mouse driver: standard 3-byte packet decoding over the 8042 controller's second (auxiliary)
// port on IRQ12. Unlike keyboard.c, which just registers an IRQ handler (the first PS/2 port is
// already enabled by BIOS POST on real and virtual hardware alike), the second port is not
// auto-enabled — this driver has to do that handshake itself.

#include <stdint.h>
#include "io.h"
#include "irq.h"
#include "pic.h"
#include "terminal.h"
#include "mouse.h"

#define PS2_DATA_PORT 0x60
#define PS2_STATUS_PORT 0x64
#define PS2_COMMAND_PORT 0x64

#define PS2_STATUS_OUTPUT_FULL 0x01 // set when the controller has a byte ready to read
#define PS2_STATUS_INPUT_FULL 0x02  // set while the controller hasn't yet consumed our last write

static void ps2_wait_output(void)
{
    while (!(inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL))
    {
    }
}

static void ps2_wait_input(void)
{
    while (inb(PS2_STATUS_PORT) & PS2_STATUS_INPUT_FULL)
    {
    }
}

static void ps2_write_command(uint8_t cmd)
{
    ps2_wait_input();
    outb(PS2_COMMAND_PORT, cmd);
}

static void ps2_write_data(uint8_t data)
{
    ps2_wait_input();
    outb(PS2_DATA_PORT, data);
}

static uint8_t ps2_read_data(void)
{
    ps2_wait_output();
    return inb(PS2_DATA_PORT);
}

// Sends a command byte to the mouse itself, routed through the controller's second port, and
// reads back its ACK (0xFA) — not checked; a real driver would retry on NAK, out of scope here
static void mouse_send_command(uint8_t cmd)
{
    ps2_write_command(0xD4); // "next data-port byte goes to the second (mouse) port"
    ps2_write_data(cmd);
    ps2_read_data();
}

#define MOUSE_BUFFER_SIZE 64
static struct mouse_packet mouse_buffer[MOUSE_BUFFER_SIZE];
static volatile int mouse_head = 0;
static volatile int mouse_tail = 0;

static void mouse_buffer_push(struct mouse_packet p)
{
    int next = (mouse_head + 1) % MOUSE_BUFFER_SIZE;
    if (next != mouse_tail)
    {
        mouse_buffer[mouse_head] = p;
        mouse_head = next;
    }
}

// Raw bytes accumulate here across IRQs — one full packet is 3 bytes, delivered one at a time
static uint8_t packet_bytes[3];
static int packet_index = 0;

static void mouse_handler(void)
{
    uint8_t data = inb(PS2_DATA_PORT);

    if (packet_index == 0 && !(data & 0x08)) // bit 3 is always set on a real byte 0 — a cheap
                                              // sync check; if it's clear, this can't be a byte 0
    {
        return; // out of sync — drop it and keep waiting for a real byte 0
    }

    packet_bytes[packet_index++] = data;
    if (packet_index < 3)
    {
        return; // packet not complete yet
    }
    packet_index = 0;

    uint8_t flags = packet_bytes[0];

    struct mouse_packet p;
    p.left_button = flags & 0x01;
    p.right_button = (flags & 0x02) != 0;
    p.middle_button = (flags & 0x04) != 0;

    // Movement is a signed 9-bit value: an 8-bit magnitude plus a sign bit in the flags byte.
    // When the sign bit is set, the byte is already the low 8 bits of a two's-complement value,
    // so subtracting 256 sign-extends it correctly. An overflow bit means the counter wrapped —
    // the value is meaningless, so that axis is reported as no movement rather than a bogus jump.
    p.dx = (flags & 0x40) ? 0 : ((flags & 0x10) ? (int32_t)packet_bytes[1] - 256 : (int32_t)packet_bytes[1]);
    p.dy = (flags & 0x80) ? 0 : ((flags & 0x20) ? (int32_t)packet_bytes[2] - 256 : (int32_t)packet_bytes[2]);

    mouse_buffer_push(p);
}

void mouse_install(void)
{
    ps2_write_command(0xA8); // enable the second (auxiliary/mouse) PS/2 port

    ps2_write_command(0x20); // "read controller configuration byte"
    uint8_t config = ps2_read_data();
    config |= 0x02;  // enable IRQ12 (second port interrupt)
    config &= ~0x20; // enable the second port's clock (0 = enabled — the sense is inverted)
    ps2_write_command(0x60); // "write controller configuration byte"
    ps2_write_data(config);

    mouse_send_command(0xF6); // "use default settings" — a clean, known starting state
    mouse_send_command(0xF4); // "enable data reporting" — no packets arrive at all without this

    irq_set_handler(12, mouse_handler);
    pic_unmask_irq(12); // nothing has used this line before — don't assume the BIOS left it
                         // unmasked, same lesson as IRQ4/COM1 from the serial RX work

    terminal_writestring("Mouse: PS/2 mouse initialized\n");
}

int mouse_has_packet(void)
{
    return mouse_head != mouse_tail;
}

struct mouse_packet mouse_read_packet(void)
{
    __asm__ volatile("sti"); // this may run with IF=0 if called from inside a syscall

    while (mouse_head == mouse_tail)
    {
        __asm__ volatile("hlt");
    }

    struct mouse_packet p = mouse_buffer[mouse_tail];
    mouse_tail = (mouse_tail + 1) % MOUSE_BUFFER_SIZE;
    return p;
}
