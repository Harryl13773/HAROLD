// Reads and prints a file — proves a file written by save.elf really persisted to disk.
// Takes an optional filename (argv[1]); defaults to saved.txt with no arguments.

#include "libc.h"

void _start(int argc, char **argv)
{
    const char *filename = (argc >= 2) ? argv[1] : "saved.txt";
    int fd = open(filename);
    if (fd < 0)
    {
        const char *err = "readsaved: ";
        write(err, strlen(err));
        write(filename, strlen(filename));
        const char *err2 = " not found\n";
        write(err2, strlen(err2));
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
