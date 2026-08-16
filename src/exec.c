// Lets a ring-3 program launch an ELF and wait for it to finish

#include "task.h"
#include "elf.h"
#include "exec.h"

#define EXEC_MAX_ARGS 16
#define EXEC_MAX_ARG_LEN 64

// Passes launch arguments to the new task before exec_run() waits for it
static char exec_arg_storage[EXEC_MAX_ARGS][EXEC_MAX_ARG_LEN];
static char *exec_argv[EXEC_MAX_ARGS];
static int exec_argc;

// Runs as its own task so exec_run's caller stays blocked until the launched program exits
static void exec_launch_trampoline(void)
{
    elf_load_and_run(exec_argv[0], exec_argc, exec_argv);
}

// Launches argv[0] as an ELF program with the rest as its arguments, and blocks until it exits
int exec_run(int argc, char *const argv[])
{
    if (argc < 1 || argc > EXEC_MAX_ARGS)
    {
        return -1;
    }

    for (int i = 0; i < argc; i++)
    {
        int len = 0;
        while (argv[i][len] != '\0' && len < EXEC_MAX_ARG_LEN - 1)
        {
            exec_arg_storage[i][len] = argv[i][len];
            len++;
        }
        exec_arg_storage[i][len] = '\0';
        exec_argv[i] = exec_arg_storage[i];
    }
    exec_argc = argc;

    int child_id = task_create(exec_launch_trampoline);
    if (child_id < 0)
    {
        return -1;
    }

    task_wait(child_id);
    return 0;
}
