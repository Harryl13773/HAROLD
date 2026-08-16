/*
Reads the same two raw addresses writer.elf wrote its secrets to, without ever writing to
either first — if isolation is working, both read whatever a fresh physical frame contains,
not the secret.
*/

#include "libc.h"

// Prints label, then whatever is actually at raw (up to 32 bytes or a NUL)
static void read_and_report(const char *label, volatile char *raw)
{
    write(label, strlen(label));

    int len = 0;
    while (raw[len] != '\0' && len < 32)
    {
        len++;
    }
    write((char *)raw, (unsigned int)len);

    const char *end = "'\n";
    write(end, strlen(end));
}

void _start(void)
{
    read_and_report("spy: reading 0x300800 without writing first: '", (volatile char *)0x300800);
    read_and_report("spy: reading 0x3ff800 without writing first: '", (volatile char *)0x3FF800);

    exit();

    for (;;)
    {
    }
}