// Reads the same raw address writer.elf wrote its secret to, without ever writing to it first —
// if isolation is working, this reads whatever a fresh physical frame contains, not the secret

#include "libc.h"

void _start(void)
{
    volatile char *raw = (volatile char *)0x300800;

    const char *msg = "spy: reading 0x300800 without writing first: '";
    write(msg, strlen(msg));

    int len = 0;
    while (raw[len] != '\0' && len < 32)
    {
        len++;
    }
    write((char *)raw, (unsigned int)len);

    const char *end = "'\n";
    write(end, strlen(end));

    exit();

    for (;;)
    {
    }
}