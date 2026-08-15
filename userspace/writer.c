// Writes a secret to two raw addresses outside any variable we declare — one inside our own ELF
// segment, one inside our own stack page — then exits. spy.elf checks afterward whether either is
// still visible to a different process: the segment address was already private before the
// stack/heap isolation fix, the stack address is what that fix is actually supposed to protect.

#include "libc.h"

void _start(void)
{
    volatile char *raw = (volatile char *)0x300800;      // inside our own ELF segment
    volatile char *stack_raw = (volatile char *)0x3FF800; // inside our own stack page (USER_STACK_VADDR + 0x800)

    const char *secret = "TOPSECRET12345";
    int i = 0;
    while (secret[i] != '\0')
    {
        raw[i] = secret[i];
        stack_raw[i] = secret[i];
        i++;
    }
    raw[i] = '\0';
    stack_raw[i] = '\0';

    const char *msg = "writer: wrote secrets to 0x300800 and 0x3ff800, exiting\n";
    write(msg, strlen(msg));

    exit();

    for (;;)
    {
    }
}