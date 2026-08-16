// Resolves a hostname to an IPv4 address via dns_resolve() and prints it dotted-quad — proves the
// DNS resolver and gateway-aware routing work end-to-end, not just in isolation

#include "libc.h"

static void print_ip(unsigned char ip[4])
{
    char num[12];
    for (int i = 0; i < 4; i++)
    {
        int len = itoa(ip[i], num);
        write(num, (unsigned int)len);
        if (i < 3)
        {
            write(".", 1);
        }
    }
}

void _start(int argc, char **argv)
{
    if (argc < 2)
    {
        const char *usage = "nslookup: usage: nslookup.elf <hostname>\n";
        write(usage, strlen(usage));
        exit();
    }

    unsigned char ip[4];
    if (dns_resolve(argv[1], ip) == 0)
    {
        write(argv[1], strlen(argv[1]));
        const char *arrow = " -> ";
        write(arrow, strlen(arrow));
        print_ip(ip);
        write("\n", 1);
    }
    else
    {
        const char *err = "nslookup: could not resolve ";
        write(err, strlen(err));
        write(argv[1], strlen(argv[1]));
        write("\n", 1);
    }

    exit();

    for (;;)
    {
    }
}
