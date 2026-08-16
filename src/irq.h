// Public interface for IRQ stub installation and driver handler registration.

#ifndef IRQ_H
#define IRQ_H

typedef void (*irq_handler_t)(void);

// Registers all 16 IRQ stubs into the IDT
void irq_install(void);

// Lets a driver (keyboard, timer, etc.) attach itself to a specific IRQ line
void irq_set_handler(int irq, irq_handler_t handler);

#endif