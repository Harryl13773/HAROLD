// Interactive shell task: reads a command line from the keyboard, splits it into argv, and runs
// argv[0] as an ELF program with the rest as its arguments

#include "keyboard.h"
#include "terminal.h"
#include "task.h"
#include "elf.h"
#include "usermode.h"
#include "shell.h"

// Set by the shell right before creating the launch task — safe as a shared buffer since the
// shell always waits (task_wait) before reusing it for the next command. shell_argv points into
// shell_command_line itself (tokenize() splits it in place), so both share the same lifetime.
static char shell_command_line[64];
static char *shell_argv[MAX_ARGS];
static int shell_argc;

// Runs as its own task so the shell survives after the launched program exits
static void shell_launch_trampoline(void)
{
    elf_load_and_run(shell_argv[0], shell_argc, shell_argv);
}

// Reads one line from the keyboard buffer, handling backspace properly
static int shell_read_line(char *buf, int max_len)
{
    int count = 0;

    while (count < max_len - 1)
    {
        char c = keyboard_read_char();

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

// Splits line in place on spaces into up to max_args tokens, argv[0] the program name
static int tokenize(char *line, char **argv, int max_args)
{
    int argc = 0;
    char *p = line;

    while (*p != '\0' && argc < max_args)
    {
        while (*p == ' ')
        {
            p++;
        }
        if (*p == '\0')
        {
            break;
        }

        argv[argc++] = p;

        while (*p != '\0' && *p != ' ')
        {
            p++;
        }
        if (*p == ' ')
        {
            *p = '\0';
            p++;
        }
    }

    return argc;
}

void shell_task(void)
{
    terminal_writestring("\nHAROLD shell - type a filename (e.g. test.elf) to run it, optionally followed by arguments\n");

    while (1)
    {
        terminal_writestring("> ");

        int len = shell_read_line(shell_command_line, sizeof(shell_command_line));

        if (len == 0)
        {
            continue; // empty command, just reprompt
        }

        shell_argc = tokenize(shell_command_line, shell_argv, MAX_ARGS);
        if (shell_argc == 0)
        {
            continue; // all-whitespace input
        }

        int child_id = task_create(shell_launch_trampoline);
        if (child_id < 0)
        {
            terminal_writestring("shell: could not create task\n");
            continue;
        }

        task_wait(child_id);
    }
}
