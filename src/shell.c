#include "keyboard.h"
#include "terminal.h"
#include "task.h"
#include "elf.h"
#include "shell.h"

// Set by the shell right before creating the launch task — safe as a shared buffer since the shell always waits before reusing it
static char shell_program_name[64];

// Runs as its own task so the shell survives after the launched program exits
static void shell_launch_trampoline(void)
{
    elf_load_and_run(shell_program_name);
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

void shell_task(void)
{
    terminal_writestring("\nHAROLD shell - type a filename (e.g. test.elf) to run it\n");

    while (1)
    {
        terminal_writestring("> ");

        int len = shell_read_line(shell_program_name, sizeof(shell_program_name));

        if (len == 0)
        {
            continue; // empty command, just reprompt
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