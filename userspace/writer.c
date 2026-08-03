// Writes a secret to a raw address outside any variable we declare, then exits — spy.elf checks
// afterward whether that memory is still visible to a different process

#include "libc.h"

void _start(void)
{
    volatile char *raw = (volatile char *)0x300800; // deliberately outside our own declared variables

    const char *secret = "TOPSECRET12345";
    int i = 0;
    while (secret[i] != '\0')
    {
        raw[i] = secret[i];
        i++;
    }
    raw[i] = '\0';

    const char *msg = "writer: wrote a secret to 0x300800, exiting\n";
    write(msg, strlen(msg));

    exit();

    for (;;)
    {
    }
}