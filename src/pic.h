// Public interface for remapping the 8259 PICs

#ifndef PIC_H
#define PIC_H

// Reinitializes both PICs so hardware IRQs map to vectors 32-47 instead of 0-15
void pic_remap(void);

// Clears an IRQ line's mask bit so the PIC actually delivers it. pic_remap() only preserves
// whatever mask was already set (typically left by the BIOS) — a line nothing has ever explicitly
// needed before (e.g. IRQ4/COM1) isn't guaranteed to already be unmasked, unlike keyboard/PIT/NIC
// which demonstrably are.
void pic_unmask_irq(int irq);

#endif