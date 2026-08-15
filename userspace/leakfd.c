// Opens readme.txt and deliberately exits without ever closing it — run this more than
// MAX_OPEN_FILES (8) times in a row; if leaked descriptors aren't reclaimed, the 9th run fails

#include "libc.h"

void _start(void)
{
    int fd = open("readme.txt");
    if (fd < 0)
    {
        const char *err = "leakfd: could not open readme.txt\n";
        write(err, strlen(err));
        exit();
    }

    const char *ok = "leakfd: opened fd ";
    write(ok, strlen(ok));
    char num[12];
    int len = itoa(fd, num);
    write(num, (unsigned int)len);
    const char *ok2 = ", exiting without closing it\n";
    write(ok2, strlen(ok2));

    // Deliberately no close(fd) here — task_reap is what should clean this up instead

    exit();

    for (;;)
    {
    }
}