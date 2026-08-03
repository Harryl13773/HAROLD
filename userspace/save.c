// Creates a new file and writes a fixed message into it — proves real FAT write support

#include "libc.h"

void _start(void)
{
    const char *filename = "saved.txt";
    int fd = open_write(filename, FAT_OPEN_CREATE);
    if (fd < 0)
    {
        const char *err = "save: could not create saved.txt (already exists?)\n";
        write(err, strlen(err));
        exit();
    }

    const char *message = "Hello from a real saved file!\n";
    int written = fwrite(fd, message, strlen(message));
    close(fd);

    const char *ok = "save: wrote saved.txt (";
    write(ok, strlen(ok));
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