// Overwrites saved.txt with new, shorter content — proves FAT_OPEN_TRUNCATE really empties the old file

#include "libc.h"

void _start(void)
{
    const char *filename = "saved.txt";
    int fd = open_write(filename, FAT_OPEN_TRUNCATE);
    if (fd < 0)
    {
        const char *err = "overwrite: could not open saved.txt for truncate\n";
        write(err, strlen(err));
        exit();
    }

    const char *message = "Replaced.\n";
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