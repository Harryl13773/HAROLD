// Public interface for the PS/2 mouse driver.

#ifndef MOUSE_H
#define MOUSE_H

#include <stdint.h>

// One decoded 3-byte PS/2 packet: relative movement since the last packet, plus button state.
// Reported exactly as the hardware sends it, with no coordinate-system opinion imposed — notably,
// PS/2's own convention is that positive dy means the mouse moved UP, the opposite of typical
// screen coordinates (where Y increases downward). A cursor/GUI layer consuming this is
// responsible for negating dy itself if it wants "down is positive" screen semantics; this driver
// deliberately doesn't guess that for it.
struct mouse_packet
{
    int32_t dx;
    int32_t dy;
    int32_t left_button;
    int32_t right_button;
    int32_t middle_button;
};

// Enables the second PS/2 port for a mouse, sets default settings, enables data reporting, and
// wires up its IRQ12 handler — call once at boot, after irq_install()/pic_remap()
void mouse_install(void);

// True if a decoded packet is waiting in the buffer without blocking
int mouse_has_packet(void);

// Blocks (via hlt, not a busy-spin) until a packet is available, then returns it
struct mouse_packet mouse_read_packet(void);

#endif
