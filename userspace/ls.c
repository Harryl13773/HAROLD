// Lists every file in the root directory via listdir(), one index at a
// time until it returns -1 past the last entry

#include "libc.h"

static void append(char *line, int *len, const char *s)
{
    while (*s != '\0')
    {
        line[(*len)++] = *s++;
    }
}

static void append_int(char *line, int *len, int value)
{
    char numbuf[12];
    int nlen = itoa(value, numbuf);
    for (int i = 0; i < nlen; i++)
    {
        line[(*len)++] = numbuf[i];
    }
}

void _start(void)
{
    const char *banner = "Files on disk:\n";
    write(banner, strlen(banner));

    char name[64];
    int index = 0;

    while (1)
    {
        int size = listdir(index, name, sizeof(name));
        if (size < 0)
        {
            break;
        }

        char line[96];
        int len = 0;
        append(line, &len, "  ");
        append(line, &len, name);
        append(line, &len, "  (");
        append_int(line, &len, size);
        append(line, &len, " bytes)\n");
        write(line, len);

        index++;
    }

    exit();

    for (;;)
    {
    }
}