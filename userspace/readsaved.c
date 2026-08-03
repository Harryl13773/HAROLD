// Reads and prints saved.txt — proves a file written by save.elf really persisted to disk

#include "libc.h"

void _start(void)
{
    int fd = open("saved.txt");
    if (fd < 0)
    {
        const char *err = "readsaved: saved.txt not found\n";
        write(err, strlen(err));
        exit();
    }

    char buf[128];
    int n;
    while ((n = read(fd, buf, sizeof(buf))) > 0)
    {
        write(buf, (unsigned int)n);
    }

    close(fd);
    exit();

    for (;;)
    {
    }
}
