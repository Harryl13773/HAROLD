/*
"cat" — reads a file in small 16-byte chunks to exercise open/read/close. Takes the filename
as argv[1]; falls back to bigfile.txt with no arguments, for the original no-argv test.
*/

#include "libc.h"

void _start(int argc, char **argv)
{
    const char *filename = (argc >= 2) ? argv[1] : "bigfile.txt";

    int fd = open(filename);
    if (fd < 0)
    {
        const char *err = "cat: could not open ";
        write(err, strlen(err));
        write(filename, strlen(filename));
        write("\n", 1);
        exit();
    }

    char buf[16];
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