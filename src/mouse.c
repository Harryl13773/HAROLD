/*
PS/2 mouse driver: standard 3-byte packet decoding over the 8042 controller's second (auxiliary)
port on IRQ12. Unlike keyboard.c, which just registers an IRQ handler (the first PS/2 port is
already enabled by BIOS POST on real and virtual hardware alike), the second port is not
auto-enabled, this driver has to do that handshake itself.
*/

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

// Blocks until the controller has a byte ready to read
static void ps2_wait_output(void)
{
    while (!(inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL))
    {
    }
}

// Blocks until the controller is ready to accept a byte
static void ps2_wait_input(void)
{
    while (inb(PS2_STATUS_PORT) & PS2_STATUS_INPUT_FULL)
    {
    }
}

// Sends a command byte to the 8042 controller itself
static void ps2_write_command(uint8_t cmd)
{
    ps2_wait_input();
    outb(PS2_COMMAND_PORT, cmd);
}

// Sends a data byte to the currently selected PS/2 port
static void ps2_write_data(uint8_t data)
{
    ps2_wait_input();
    outb(PS2_DATA_PORT, data);
}

// Reads one byte back from the controller
static uint8_t ps2_read_data(void)
{
    ps2_wait_output();
    return inb(PS2_DATA_PORT);
}

// Sends a mouse command through the controller and reads its ACK
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

// Queues a decoded packet for mouse_read_packet; drops it if the buffer is full
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

// Fires on every IRQ12 — accumulates one 3-byte packet, then decodes and queues it
static void mouse_handler(void)
{
    uint8_t data = inb(PS2_DATA_PORT);

    if (packet_index == 0 && !(data & 0x08)) // Bit 3 must be set in byte 0, providing a simple sync check
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

    // Decode signed movement; ignore axes with overflow to prevent bogus jumps
    p.dx = (flags & 0x40) ? 0 : ((flags & 0x10) ? (int32_t)packet_bytes[1] - 256 : (int32_t)packet_bytes[1]);
    p.dy = (flags & 0x80) ? 0 : ((flags & 0x20) ? (int32_t)packet_bytes[2] - 256 : (int32_t)packet_bytes[2]);

    mouse_buffer_push(p);
}

// Initializes the PS/2 mouse and IRQ12 handler; call after IRQ/PIC setup
void mouse_install(void)
{
    ps2_write_command(0xA8); // enable the second (auxiliary/mouse) PS/2 port

    // Enable the second port clock, but keep IRQ12 off until the mouse handshake completes
    ps2_write_command(0x20); // "read controller configuration byte"
    uint8_t config = ps2_read_data();
    config &= ~0x20;         // enable the second port's clock
    ps2_write_command(0x60); // "write controller configuration byte"
    ps2_write_data(config);

    mouse_send_command(0xF6); // "use default settings" — a clean, known starting state
    mouse_send_command(0xF4); // "enable data reporting" — no packets arrive at all without this

    irq_set_handler(12, mouse_handler);

    ps2_write_command(0x20);
    config = ps2_read_data();
    config |= 0x02; //// Enable IRQ12 after the handshake to avoid stale interrupts
    ps2_write_command(0x60);
    ps2_write_data(config);

    pic_unmask_irq(12); // Explicitly unmask IRQ12 rather than relying on the BIOS state

    terminal_writestring("Mouse: PS/2 mouse initialized\n");
}

// True if a decoded packet is waiting in the buffer without blocking
int mouse_has_packet(void)
{
    return mouse_head != mouse_tail;
}

// Blocks (via hlt, not a busy-spin) until a packet is available, then returns it
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
