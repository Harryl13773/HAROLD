// Public interface for installing CPU exception handlers

#ifndef ISR_H
#define ISR_H

// Registers all 32 exception handlers into the IDT
void isr_install(void);

#endif