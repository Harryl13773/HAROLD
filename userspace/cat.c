// Minimal "cat" — reads bigfile.txt in small 16-byte chunks to exercise open/read/close

#include "libc.h"

void _start(void)
{
    int fd = open("bigfile.txt");
    if (fd < 0)
    {
        const char *err = "cat: could not open bigfile.txt\n";
        write(err, strlen(err));
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