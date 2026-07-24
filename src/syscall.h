#ifndef SYSCALL_H
#define SYSCALL_H

// Registers the int 0x80 gate at DPL 3, so ring 3 code can invoke it
void syscall_install(void);

#endif