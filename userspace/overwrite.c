/*
Overwrites a file with new, shorter content — proves FAT_OPEN_TRUNCATE really empties the old
file. Takes an optional filename (argv[1]) and message (argv[2]); defaults to saved.txt and
the original fixed message with no arguments.
*/

#include "libc.h"

void _start(int argc, char **argv)
{
    const char *filename = (argc >= 2) ? argv[1] : "saved.txt";
    int fd = open_write(filename, FAT_OPEN_TRUNCATE);
    if (fd < 0)
    {
        const char *err = "overwrite: could not open ";
        write(err, strlen(err));
        write(filename, strlen(filename));
        const char *err2 = " for truncate\n";
        write(err2, strlen(err2));
        exit();
    }

    const char *message = (argc >= 3) ? argv[2] : "Replaced.\n";
    int written = fwrite(fd, message, strlen(message));
    close(fd);

    const char *ok = "overwrite: wrote ";
    write(ok, strlen(ok));
    char num[12];
    int len = itoa(written, num);
    write(num, (unsigned int)len);
    const char *ok2 = " bytes\n";
    write(ok2, strlen(ok2));

    exit();

    for (;;)
    {
    }
}