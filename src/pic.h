#ifndef PIC_H
#define PIC_H

// Reinitializes both PICs so hardware IRQs map to vectors 32-47 instead of 0-15
void pic_remap(void);

#endif