// Public interface for remapping the 8259 PICs.

#ifndef PIC_H
#define PIC_H

// Reinitializes both PICs so hardware IRQs map to vectors 32-47 instead of 0-15
void pic_remap(void);

// Unmasks an IRQ line so the PIC can deliver it
void pic_unmask_irq(int irq);

#endif