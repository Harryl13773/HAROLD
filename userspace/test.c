// Basic ELF smoke test: prints a greeting, reads a line from stdin, and echoes it back

#include "libc.h"

void _start(void)
{
    const char *msg = "Hello from an ELF binary!\n";
    write(msg, strlen(msg));

    const char *prompt = "Type something and press enter: ";
    write(prompt, strlen(prompt));

    char input[64];
    int n = read(0, input, sizeof(input));

    const char *echo = "You typed: ";
    write(echo, strlen(echo));
    write(input, (unsigned int)n);

    exit();

    for (;;)
    {
    }
}