#include <stdint.h>
#include "io.h"
#include "irq.h"
#include "task.h"
#include "pit.h"

#define PIT_CHANNEL0 0x40
#define PIT_COMMAND 0x43
#define PIT_BASE_FREQ 1193182

static volatile uint32_t tick_count = 0;

// Fires on every IRQ0 — counts ticks and hands off to the scheduler
static void pit_handler(void)
{
    tick_count++;
    schedule();
}

void pit_init(uint32_t frequency)
{
    uint32_t divisor = PIT_BASE_FREQ / frequency;

    outb(PIT_COMMAND, 0x36);
    outb(PIT_CHANNEL0, divisor & 0xFF);
    outb(PIT_CHANNEL0, (divisor >> 8) & 0xFF);

    irq_set_handler(0, pit_handler);
}

uint32_t pit_get_ticks(void)
{
    return tick_count;
}