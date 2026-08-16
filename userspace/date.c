// Prints the current wall-clock date/time via rtc_read() — proves the RTC driver end-to-end.

#include "libc.h"

// Writes value zero-padded to width digits (itoa alone doesn't pad, e.g. "5" not "05")
static void write_padded(int value, int width)
{
    char num[12];
    int len = itoa(value, num);

    for (int i = len; i < width; i++)
    {
        write("0", 1);
    }
    write(num, (unsigned int)len);
}

void _start(void)
{
    struct rtc_time t;
    rtc_read(&t);

    write_padded(t.year, 4);
    write("-", 1);
    write_padded(t.month, 2);
    write("-", 1);
    write_padded(t.day, 2);
    write(" ", 1);
    write_padded(t.hour, 2);
    write(":", 1);
    write_padded(t.minute, 2);
    write(":", 1);
    write_padded(t.second, 2);
    write("\n", 1);

    exit();

    for (;;)
    {
    }
}
