// Public interface for installing the CPU's 32 exception handlers into the IDT.

#ifndef ISR_H
#define ISR_H

// Registers all 32 exception handlers into the IDT
void isr_install(void);

#endif