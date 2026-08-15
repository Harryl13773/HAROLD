// Appends another line to a file — proves FAT_OPEN_APPEND resumes from the file's real end.
// Takes an optional filename (argv[1]) and message (argv[2]); defaults to saved.txt and the
// original fixed message with no arguments.

#include "libc.h"

void _start(int argc, char **argv)
{
    const char *filename = (argc >= 2) ? argv[1] : "saved.txt";
    int fd = open_write(filename, FAT_OPEN_APPEND);
    if (fd < 0)
    {
        const char *err = "append: could not open ";
        write(err, strlen(err));
        write(filename, strlen(filename));
        const char *err2 = " for append\n";
        write(err2, strlen(err2));
        exit();
    }

    const char *message = (argc >= 3) ? argv[2] : "This line was appended.\n";
    int written = fwrite(fd, message, strlen(message));
    close(fd);

    const char *ok = "append: appended ";
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