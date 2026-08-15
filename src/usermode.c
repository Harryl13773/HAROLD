// Allocates a user-mode stack, lays out a real argc/argv call frame on it, and performs the
// ring0->ring3 jump into a task's entry point

#include <stdint.h>
#include "heap.h"
#include "paging.h"
#include "pmm.h"
#include "task.h"
#include "usermode.h"

#define USER_STACK_SIZE 4096

extern void jump_to_usermode(uint32_t entry_eip, uint32_t user_esp);

void usermode_enter(void (*entry)(void), int argc, char *const argv[])
{
    // A genuinely private frame for this task's stack, mapped PAGE_USER at a fixed VA in the
    // same private table its ELF segment already lives in — same treatment elf.c gives the
    // segment itself, so no other task can read or write it. Freed automatically by
    // paging_free_user_directory() in task_reap(), same as the segment's frames are.
    uint32_t stack_frame = pmm_alloc_frame();
    if (stack_frame == 0)
    {
        return;
    }

    uint8_t *frame_ptr = (uint8_t *)stack_frame;
    for (uint32_t z = 0; z < USER_STACK_SIZE; z++)
    {
        frame_ptr[z] = 0;
    }

    uint32_t *dir = task_get_current_page_directory();
    uint32_t *table = paging_get_or_create_user_table(dir, USER_STACK_VADDR);
    if (table == NULL)
    {
        pmm_free_frame(stack_frame);
        return;
    }

    paging_map_user_page(table, USER_STACK_VADDR, stack_frame);
    paging_switch_directory(dir); // flush stale TLB entries before this VA is used, same as elf.c does

    uint8_t *stack = (uint8_t *)USER_STACK_VADDR;
    uint8_t *top = stack + USER_STACK_SIZE;

    if (argc > MAX_ARGS)
    {
        argc = MAX_ARGS; // silently truncate rather than fail — matches this shell's own line-length cap
    }

    // Copy each argument string onto the user stack itself — already privately mapped above —
    // so the new process can read its own argv with no additional mapping work
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
