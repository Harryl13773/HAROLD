// Public interface for the PS/2 mouse driver.

#ifndef MOUSE_H
#define MOUSE_H

#include <stdint.h>

// Decoded PS/2 packet with relative movement and button state; positive dy means up
struct mouse_packet
{
    int32_t dx;
    int32_t dy;
    int32_t left_button;
    int32_t right_button;
    int32_t middle_button;
};

// Initializes the PS/2 mouse and IRQ12 handler; call after IRQ/PIC setup
void mouse_install(void);

// True if a decoded packet is waiting in the buffer without blocking
int mouse_has_packet(void);

// Blocks (via hlt, not a busy-spin) until a packet is available, then returns it
struct mouse_packet mouse_read_packet(void);

#endif
