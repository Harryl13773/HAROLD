#ifndef IDT_H
#define IDT_H

// Builds and loads the IDT
void idt_install(void);

// Fills in one IDT entry — used by isr.c to register exception handlers
void idt_set_gate(int num, uint32_t base, uint16_t sel, uint8_t flags);

#endif