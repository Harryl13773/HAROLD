// Lists every entry in a directory via listdir(), one index at a time until it returns -1 past
// the last entry. argv[1] is an optional directory path ("" / omitted means root), argv[2] an
// optional substring filter within it.

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

static char to_lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

// True if needle appears anywhere in haystack, case-insensitively — this FAT driver's directory
// entries mix case inconsistently (short 8.3 names vs. case-preserved long names), so a
// case-sensitive filter would silently miss real matches; libc only has exact, case-sensitive strcmp
static int contains(const char *haystack, const char *needle)
{
    for (int i = 0; haystack[i] != '\0'; i++)
    {
        int j = 0;
        while (needle[j] != '\0' && to_lower(haystack[i + j]) == to_lower(needle[j]))
        {
            j++;
        }
        if (needle[j] == '\0')
        {
            return 1;
        }
    }
    return 0;
}

void _start(int argc, char **argv)
{
    const char *dir_path = (argc >= 2) ? argv[1] : "";
    const char *filter = (argc >= 3) ? argv[2] : 0;

    const char *banner = "Files on disk:\n";
    write(banner, strlen(banner));

    char name[64];
    int index = 0;

    while (1)
    {
        int size = listdir(dir_path, index, name, sizeof(name));
        if (size < 0)
        {
            break;
        }

        index++;

        if (filter != 0 && !contains(name, filter))
        {
            continue;
        }

        char line[96];
        int len = 0;
        append(line, &len, "  ");
        append(line, &len, name);
        append(line, &len, "  (");
        append_int(line, &len, size);
        append(line, &len, " bytes)\n");
        write(line, len);
    }

    exit();

    for (;;)
    {
    }
}