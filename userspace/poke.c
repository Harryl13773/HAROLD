// Writes text at a specific byte offset inside an existing file, in place — proves fat_seek +
// FAT_OPEN_MODIFY actually overwrite without truncating: bytes before the offset and after the
// written text stay untouched, unlike overwrite.elf (which always empties the whole file first)

#include "libc.h"

void _start(int argc, char **argv)
{
    if (argc < 4)
    {
        const char *usage = "poke: usage: poke.elf <filename> <offset> <text>\n";
        write(usage, strlen(usage));
        exit();
    }

    const char *filename = argv[1];
    unsigned int offset = (unsigned int)atoi(argv[2]);
    const char *text = argv[3];

    int fd = open_write(filename, FAT_OPEN_MODIFY);
    if (fd < 0)
    {
        const char *err = "poke: could not open ";
        write(err, strlen(err));
        write(filename, strlen(filename));
        write("\n", 1);
        exit();
    }

    if (seek(fd, offset) != 0)
    {
        const char *err = "poke: seek past end of file\n";
        write(err, strlen(err));
        close(fd);
        exit();
    }

    int written = fwrite(fd, text, strlen(text));
    close(fd);

    const char *msg = "poke: wrote ";
    write(msg, strlen(msg));
    char num[12];
    int len = itoa(written, num);
    write(num, (unsigned int)len);
    const char *msg2 = " bytes at offset ";
    write(msg2, strlen(msg2));
    len = itoa((int)offset, num);
    write(num, (unsigned int)len);
    write("\n", 1);

    exit();

    for (;;)
    {
    }
}
