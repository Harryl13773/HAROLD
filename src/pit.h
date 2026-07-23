#ifndef PIT_H
#define PIT_H

#include <stdint.h>

// Configures the PIT to fire IRQ0 at the given frequency, in Hz
void pit_init(uint32_t frequency);

// Returns how many ticks have occurred since boot
uint32_t pit_get_ticks(void);

#endif