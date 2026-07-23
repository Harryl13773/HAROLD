#include <stdint.h>
#include "io.h"
#include "pic.h"

#define PIC1_COMMAND 0x20 // master PIC command port
#define PIC1_DATA 0x21    // master PIC data port
#define PIC2_COMMAND 0xA0 // slave PIC command port
#define PIC2_DATA 0xA1    // slave PIC data port

#define ICW1_INIT 0x10 // begin the PIC initialization sequence
#define ICW1_ICW4 0x01 // signal that ICW4 will also be sent
#define ICW4_8086 0x01 // operate in 8086 mode

void pic_remap(void)
{
    uint8_t mask1 = inb(PIC1_DATA); // save currently masked IRQs
    uint8_t mask2 = inb(PIC2_DATA);

    outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4); // start init, master
    outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4); // start init, slave

    outb(PIC1_DATA, 0x20); // master IRQs now start at vector 32
    outb(PIC2_DATA, 0x28); // slave IRQs now start at vector 40

    outb(PIC1_DATA, 0x04); // tell master: slave is wired to IRQ2
    outb(PIC2_DATA, 0x02); // tell slave: its cascade identity is 2

    outb(PIC1_DATA, ICW4_8086); // set 8086 mode, master
    outb(PIC2_DATA, ICW4_8086); // set 8086 mode, slave

    outb(PIC1_DATA, mask1); // restore saved masks
    outb(PIC2_DATA, mask2);
}