// Allocates a user-mode stack, lays out a real argc/argv call frame on it, and performs the
// ring0->ring3 jump into a task's entry point

#include <stdint.h>
#include "heap.h"
#include "usermode.h"

#define USER_STACK_SIZE 4096

extern void jump_to_usermode(uint32_t entry_eip, uint32_t user_esp);

void usermode_enter(void (*entry)(void), int argc, char *const argv[])
{
    // Leaked by design for now — no process teardown exists yet to free it against
    uint8_t *stack = (uint8_t *)kmalloc(USER_STACK_SIZE);
    if (stack == NULL)
    {
        return;
    }

    uint8_t *top = stack + USER_STACK_SIZE;

    if (argc > MAX_ARGS)
    {
        argc = MAX_ARGS; // silently truncate rather than fail — matches this shell's own line-length cap
    }

    // Copy each argument string onto the user stack itself (already kmalloc'd, already mapped
    // PAGE_USER for every task via the shared first_page_table) so the new process can read its
    // own argv without needing any additional private mapping
    char *copied[MAX_ARGS];
    for (int i = argc - 1; i >= 0; i--)
    {
        const char *src = argv[i];
        uint32_t len = 0;
        while (src[len] != '\0')
        {
            len++;
        }
        len++; // include the NUL

        top -= len;
        for (uint32_t b = 0; b < len; b++)
        {
            top[b] = src[b];
        }
        copied[i] = (char *)top;
    }

    top = (uint8_t *)((uint32_t)top & ~3u); // keep the pointer array below this naturally aligned

    // The argv pointer array itself, argc entries plus a NULL terminator, C convention
    top -= (uint32_t)(argc + 1) * sizeof(char *);
    char **argv_array = (char **)top;
    for (int i = 0; i < argc; i++)
    {
        argv_array[i] = copied[i];
    }
    argv_array[argc] = NULL;

    // A fake cdecl call frame — [return address][argc][argv] — so a normal
    // void _start(int argc, char **argv) reads them exactly as if it had been `call`ed rather than
    // jumped to directly. The fake return address is 0: if a program's _start ever falls through
    // instead of calling exit(), "returning" here page-faults, which fault_handler already turns
    // into a clean ring-3 task termination instead of a halt.
    uint32_t *frame = (uint32_t *)top;
    *(--frame) = (uint32_t)argv_array;
    *(--frame) = (uint32_t)argc;
    *(--frame) = 0;

    uint32_t user_esp = (uint32_t)frame;
    jump_to_usermode((uint32_t)entry, user_esp);
}
