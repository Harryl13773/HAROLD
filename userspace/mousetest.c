// Reads a bounded number of mouse packets and prints each one's movement/button state — proves
// the PS/2 mouse driver end-to-end (move the mouse or click to generate packets).

#include "libc.h"

// Prints a signed integer in decimal
static void print_int(int value)
{
    char buf[12];
    int len = itoa(value, buf);
    write(buf, (unsigned int)len);
}

void _start(void)
{
    const char *msg = "mousetest: reading 10 packets (move the mouse or click)\n";
    write(msg, strlen(msg));

    for (int i = 0; i < 10; i++)
    {
        struct mouse_packet p;
        mouse_read(&p);

        const char *dx_label = "dx=";
        write(dx_label, strlen(dx_label));
        print_int(p.dx);

        const char *dy_label = " dy=";
        write(dy_label, strlen(dy_label));
        print_int(p.dy);

        const char *buttons_label = " buttons=";
        write(buttons_label, strlen(buttons_label));
        if (p.left_button)
        {
            write("L", 1);
        }
        if (p.right_button)
        {
            write("R", 1);
        }
        if (p.middle_button)
        {
            write("M", 1);
        }
        write("\n", 1);
    }

    exit();

    for (;;)
    {
    }
}
