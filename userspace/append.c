// Appends another line to saved.txt — proves FAT_OPEN_APPEND resumes from the file's real end

#include "libc.h"

void _start(void)
{
    const char *filename = "saved.txt";
    int fd = open_write(filename, FAT_OPEN_APPEND);
    if (fd < 0)
    {
        const char *err = "append: could not open saved.txt for append\n";
        write(err, strlen(err));
        exit();
    }

    const char *message = "This line was appended.\n";
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