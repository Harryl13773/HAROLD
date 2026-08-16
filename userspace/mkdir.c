/*Creates a new, empty subdirectory — proves the FAT driver's subdirectory support end-to-end
when combined with any existing file-I/O program pointed at a path inside it (e.g.
"save.elf docs/notes.txt hello", "ls.elf docs").
*/

#include "libc.h"

void _start(int argc, char **argv)
{
    if (argc < 2)
    {
        const char *usage = "mkdir: usage: mkdir.elf <path>\n";
        write(usage, strlen(usage));
        exit();
    }

    if (mkdir(argv[1]) == 0)
    {
        const char *ok = "mkdir: created ";
        write(ok, strlen(ok));
        write(argv[1], strlen(argv[1]));
        write("\n", 1);
    }
    else
    {
        const char *err = "mkdir: could not create ";
        write(err, strlen(err));
        write(argv[1], strlen(argv[1]));
        write(" (already exists, or an intermediate directory is missing?)\n",
              strlen(" (already exists, or an intermediate directory is missing?)\n"));
    }

    exit();

    for (;;)
    {
    }
}
