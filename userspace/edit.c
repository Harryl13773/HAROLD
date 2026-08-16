/*
A basic line editor for interactively viewing and editing a text file, everything else
(save/append/overwrite/poke) can only ever write a single shell argument's worth of content.
No cursor addressing or full-screen redraw, since this shell/terminal has neither, commands
operate on line numbers instead, the same way classic Unix `ed` does.
*/

#include "libc.h"

#define MAX_LINES 512
#define MAX_LINE_LEN 512
#define MAX_LOAD_SIZE 8192

static char *lines[MAX_LINES];
static int line_count = 0;
static int modified = 0;
static char load_buf[MAX_LOAD_SIZE]; // scratch space for reading the file in — too big for the 4KB user stack

// Prints a signed integer in decimal
static void print_int(int value)
{
    char buf[12];
    int len = itoa(value, buf);
    write(buf, (unsigned int)len);
}

// Reads a line from stdin with backspace handling
static int read_line(char *buf, int max_len)
{
    int count = 0;
    while (count < max_len - 1)
    {
        char c;
        if (read(0, &c, 1) <= 0)
        {
            break;
        }
        if (c == '\n')
        {
            break;
        }
        if (c == '\b')
        {
            if (count > 0)
            {
                count--;
            }
            continue;
        }
        buf[count++] = c;
    }
    buf[count] = '\0';
    return count;
}

// Allocates and returns a heap copy of s, or NULL on out-of-memory
static char *copy_string(const char *s)
{
    unsigned int len = strlen(s);
    char *copy = (char *)malloc(len + 1);
    if (copy != 0)
    {
        memcpy(copy, s, len + 1);
    }
    return copy;
}

// Prints line n (1-based) as "N: content"
static void print_line(int n) // 1-based
{
    print_int(n);
    write(": ", 2);
    write(lines[n - 1], strlen(lines[n - 1]));
    write("\n", 1);
}

// Prints every loaded line
static void cmd_list(void)
{
    for (int i = 0; i < line_count; i++)
    {
        print_line(i + 1);
    }
}

// Prints a single line by number
static void cmd_print(int n)
{
    if (n < 1 || n > line_count)
    {
        const char *err = "edit: invalid line number\n";
        write(err, strlen(err));
        return;
    }
    print_line(n);
}

// Inserts a new typed line before line `at` (1-based); at == line_count + 1 appends at the end
static void cmd_insert(int at)
{
    if (at < 1 || at > line_count + 1)
    {
        const char *err = "edit: invalid line number\n";
        write(err, strlen(err));
        return;
    }
    if (line_count >= MAX_LINES)
    {
        const char *err = "edit: too many lines\n";
        write(err, strlen(err));
        return;
    }

    char buf[MAX_LINE_LEN];
    read_line(buf, sizeof(buf));

    char *copy = copy_string(buf);
    if (copy == 0)
    {
        const char *err = "edit: out of memory\n";
        write(err, strlen(err));
        return;
    }

    for (int i = line_count; i > at - 1; i--)
    {
        lines[i] = lines[i - 1];
    }
    lines[at - 1] = copy;
    line_count++;
    modified = 1;
}

// Deletes line n (1-based), shifting later lines up
static void cmd_delete(int n)
{
    if (n < 1 || n > line_count)
    {
        const char *err = "edit: invalid line number\n";
        write(err, strlen(err));
        return;
    }

    free(lines[n - 1]);
    for (int i = n - 1; i < line_count - 1; i++)
    {
        lines[i] = lines[i + 1];
    }
    line_count--;
    modified = 1;
}

// Writes every loaded line back out to filename, truncating any existing content
static void cmd_save(const char *filename)
{
    int fd = open_write(filename, FAT_OPEN_TRUNCATE);
    if (fd < 0)
    {
        const char *err = "edit: could not open file for writing\n";
        write(err, strlen(err));
        return;
    }

    for (int i = 0; i < line_count; i++)
    {
        fwrite(fd, lines[i], strlen(lines[i]));
        fwrite(fd, "\n", 1);
    }
    close(fd);
    modified = 0;

    const char *msg = "edit: saved\n";
    write(msg, strlen(msg));
}

// Reads filename in whole and splits it into lines[], or starts empty if it doesn't exist yet
static void load_file(const char *filename)
{
    int fd = open(filename);
    if (fd < 0)
    {
        const char *msg = "edit: (new file)\n";
        write(msg, strlen(msg));
        return;
    }

    int total = 0;
    while (total < (int)sizeof(load_buf) - 1)
    {
        int n = read(fd, load_buf + total, (unsigned int)(sizeof(load_buf) - 1 - total));
        if (n <= 0)
        {
            break;
        }
        total += n;
    }
    close(fd);

    int i = 0;
    while (i < total && line_count < MAX_LINES)
    {
        int start = i;
        while (i < total && load_buf[i] != '\n')
        {
            i++;
        }
        int len = i - start;
        if (len > MAX_LINE_LEN - 1)
        {
            len = MAX_LINE_LEN - 1; // defensive clamp — refuse rather than overrun the line buffer
        }

        char *copy = (char *)malloc((unsigned int)len + 1);
        if (copy == 0)
        {
            const char *err = "edit: out of memory loading file, stopped early\n";
            write(err, strlen(err));
            break;
        }
        memcpy(copy, load_buf + start, (unsigned int)len);
        copy[len] = '\0';
        lines[line_count++] = copy;

        if (i < total && load_buf[i] == '\n')
        {
            i++;
        }
    }
}

// Prints the command reference
static void print_help(void)
{
    const char *help =
        "Commands: l (list all)  p N (print line N)  i N (insert before N)\n"
        "          a (append at end)  d N (delete line N)  w (save)  q (quit)  h (help)\n";
    write(help, strlen(help));
}

void _start(int argc, char **argv)
{
    if (argc < 2)
    {
        const char *usage = "edit: usage: edit.elf <filename>\n";
        write(usage, strlen(usage));
        exit();
    }

    const char *filename = argv[1];
    load_file(filename);

    const char *banner = "edit: editing ";
    write(banner, strlen(banner));
    write(filename, strlen(filename));
    write(" (", 2);
    print_int(line_count);
    const char *lines_word = " lines) -- type h for help\n";
    write(lines_word, strlen(lines_word));

    for (;;)
    {
        write("* ", 2);

        char cmd[MAX_LINE_LEN];
        read_line(cmd, sizeof(cmd));

        if (cmd[0] == '\0')
        {
            continue;
        }

        char op = cmd[0];
        char *p = cmd + 1;
        while (*p == ' ')
        {
            p++;
        }
        int arg = atoi(p);

        if (op == 'l')
        {
            cmd_list();
        }
        else if (op == 'h')
        {
            print_help();
        }
        else if (op == 'q')
        {
            if (modified)
            {
                const char *warn = "edit: unsaved changes -- 'w' to save, or 'q' again to discard\n";
                write(warn, strlen(warn));
                modified = 0; // a second 'q' actually quits
            }
            else
            {
                break;
            }
        }
        else if (op == 'w')
        {
            cmd_save(filename);
        }
        else if (op == 'a')
        {
            cmd_insert(line_count + 1);
        }
        else if (op == 'i')
        {
            cmd_insert(arg);
        }
        else if (op == 'd')
        {
            cmd_delete(arg);
        }
        else if (op == 'p')
        {
            cmd_print(arg);
        }
        else
        {
            const char *err = "edit: unknown command (h for help)\n";
            write(err, strlen(err));
        }
    }

    exit();

    for (;;)
    {
    }
}
