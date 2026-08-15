// Creates a new file and writes a message into it — proves real FAT write support.
// Takes an optional filename (argv[1]) and message (argv[2]); defaults to saved.txt and the
// original fixed message with no arguments.

#include "libc.h"

void _start(int argc, char **argv)
{
    const char *filename = (argc >= 2) ? argv[1] : "saved.txt";
    int fd = open_write(filename, FAT_OPEN_CREATE);
    if (fd < 0)
    {
        const char *err = "save: could not create ";
        write(err, strlen(err));
        write(filename, strlen(filename));
        const char *err2 = " (already exists?)\n";
        write(err2, strlen(err2));
        exit();
    }

    const char *message = (argc >= 3) ? argv[2] : "Hello from a real saved file!\n";
    int written = fwrite(fd, message, strlen(message));
    close(fd);

    const char *ok = "save: wrote ";
    write(ok, strlen(ok));
    write(filename, strlen(filename));
    write(" (", 2);
    char num[12];
    int len = itoa(written, num);
    write(num, (unsigned int)len);
    const char *ok2 = " bytes)\n";
    write(ok2, strlen(ok2));

    exit();

    for (;;)
    {
    }
}