// Cooperative/preemptive round-robin task scheduler: task creation, context switching, sleep, wait, and reaping

#include <stdint.h>
#include <stddef.h>
#include "task.h"
#include "io.h"
#include "heap.h"
#include "pit.h"
#include "tss.h"
#include "paging.h"
#include "pmm.h"
#include "fat.h"
#include "terminal.h"

extern void switch_task(uint32_t *old_esp, uint32_t new_esp); // defined in task_asm.asm
extern void task_launch(void);                                // defined in task_asm.asm

static struct task tasks[MAX_TASKS];
static int current_task = -1;
static int task_count = 0;

// Finds the next runnable task after 'from', wrapping around the task list
static int next_task_id(int from)
{
    for (int i = 1; i <= task_count; i++)
    {
        int id = (from + i) % task_count;
        if (tasks[id].state == TASK_READY || tasks[id].state == TASK_RUNNING)
        {
            return id;
        }
    }
    return from;
}

// Switches to task ID, also pointing the TSS at this task's own kernel stack for ring3->ring0 transitions
void task_switch_to(int id)
{
    int old = current_task;
    current_task = id;
    tasks[id].state = TASK_RUNNING;

    if (tasks[id].stack_base != NULL)
    {
        tss_set_kernel_stack((uint32_t)tasks[id].stack_base + TASK_STACK_SIZE);
    }

    if (tasks[id].page_directory != NULL)
    {
        paging_switch_directory(tasks[id].page_directory);
    }

    switch_task(&tasks[old].esp, tasks[id].esp);
}

// Ends the current task and switches to the next runnable one
void task_exit(void)
{
    tasks[current_task].state = TASK_UNUSED;

    int next = next_task_id(current_task);
    task_switch_to(next);

    for (;;)
    {
        __asm__ volatile("hlt"); // never actually reached
    }
}

// Returns the ID of the task currently running
int task_current_id(void)
{
    return current_task;
}

// Returns the page directory of the task currently running
uint32_t *task_get_current_page_directory(void)
{
    return tasks[current_task].page_directory;
}

// Frees a task's stack, open file descriptors, private page tables/data frames, and its cloned
// directory once it has exited
void task_reap(void)
{
    for (int i = 1; i < task_count; i++)
    {
        if (tasks[i].state == TASK_UNUSED && tasks[i].stack_base != NULL)
        {
            // No outer atomic block needed here, unlike earlier versions of this function: kfree,
            // fat_close_all_for_task, and pmm_free_frame (called directly and via
            // paging_free_user_directory) are all now individually self-protected against a timer
            // interrupt landing mid-call. The one invariant task_create's claim check actually
            // depends on — state == TASK_UNUSED, stack_base == NULL, and page_directory == NULL
            // never all becoming true until cleanup genuinely finishes — is preserved by leaving
            // page_directory non-NULL until the very last line below. That final assignment is a
            // single aligned store, which an interrupt can't land in the middle of, so it needs no
            // extra protection either. Don't reorder these three steps without re-checking this.
            kfree(tasks[i].stack_base);
            tasks[i].stack_base = NULL;

            fat_close_all_for_task(i); // a task that exited without calling close() itself, cleaned up here instead

            if (tasks[i].page_directory != NULL)
            {
                paging_free_user_directory(tasks[i].page_directory); // private tables/data frames first
                pmm_free_frame((uint32_t)tasks[i].page_directory);   // then the directory frame itself
                tasks[i].page_directory = NULL;
            }
        }
    }
}

// Turns the currently executing code into task 0
void tasking_init(void)
{
    for (int i = 0; i < MAX_TASKS; i++)
    {
        tasks[i].state = TASK_UNUSED;
    }

    tasks[0].id = 0;
    tasks[0].state = TASK_RUNNING;
    tasks[0].stack_base = NULL;
    tasks[0].page_directory = paging_get_kernel_directory();

    current_task = 0;
    task_count = 1;

    terminal_writestring("Tasking initialized\n");
}

// Hand-builds a new task's stack; reuses an already-reaped slot before growing task_count
int task_create(void (*entry_point)(void))
{
    // The slot scan and task_count growth below need no locking: task_create() is never called
    // concurrently with itself (only from kernel_main at boot, sequentially, and later only from
    // shell_task, which always task_wait()s for one launched task to exit before creating the
    // next), and task_reap() never touches task_count. A slot only looks free here once task_reap
    // has fully finished with it (see the comment there), so no race with reaping either.
    int id = -1;

    for (int i = 1; i < task_count; i++)
    {
        if (tasks[i].state == TASK_UNUSED && tasks[i].stack_base == NULL && tasks[i].page_directory == NULL)
        {
            id = i;
            break;
        }
    }

    if (id == -1)
    {
        if (task_count >= MAX_TASKS)
        {
            return -1;
        }
        id = task_count;
        task_count++;
    }

    // Everything below builds into locals, nothing touches tasks[id] yet — so however long the
    // kmalloc/directory clone take, task_reap() has nothing to prematurely see, because this slot
    // still reads exactly as it did before this call (fully reaped, all NULL/UNUSED).
    uint32_t *new_stack_base = (uint32_t *)kmalloc(TASK_STACK_SIZE);
    if (new_stack_base == NULL)
    {
        return -1;
    }

    uint32_t *new_page_directory = paging_clone_kernel_directory();
    if (new_page_directory == NULL)
    {
        kfree(new_stack_base); // don't leak the stack we just allocated if the directory clone fails
        return -1;
    }

    terminal_writestring("Task ");
    terminal_print_dec((uint32_t)id);
    terminal_writestring(": cloned page directory at ");
    terminal_print_hex((uint32_t)new_page_directory);
    terminal_writestring("\n");

    uint32_t *stack_top = (uint32_t *)((uint8_t *)new_stack_base + TASK_STACK_SIZE);

    *(--stack_top) = (uint32_t)entry_point; // consumed by task_launch's pop
    *(--stack_top) = (uint32_t)task_launch; // consumed by switch_task's ret
    *(--stack_top) = 0;                     // fake saved ebp
    *(--stack_top) = 0;                     // fake saved ebx
    *(--stack_top) = 0;                     // fake saved esi
    *(--stack_top) = 0;                     // fake saved edi — becomes this task's esp

    // Publish everything atomically — this is the only part of task_create that still needs
    // interrupts disabled. task_reap()'s reap condition must never see stack_base non-NULL while
    // state is still TASK_UNUSED, so all of these fields have to become visible together; every
    // field here is a plain assignment now, not a kmalloc/clone, so this is O(1), not O(setup).
    uint32_t flags = save_and_disable_interrupts();

    struct task *t = &tasks[id];
    t->stack_base = new_stack_base;
    t->page_directory = new_page_directory;
    t->esp = (uint32_t)stack_top;
    t->id = id;
    t->state = TASK_READY;

    restore_interrupts(flags);
    return id;
}

// Wakes any sleeping task whose wake_tick has passed
static void wake_sleepers(uint32_t now)
{
    for (int i = 0; i < task_count; i++)
    {
        if (tasks[i].state == TASK_SLEEPING && (int32_t)(now - tasks[i].wake_tick) >= 0)
        {
            tasks[i].state = TASK_READY;
        }
    }
}

// Called from the PIT handler every tick — the round-robin switch
void schedule(void)
{
    wake_sleepers(pit_get_ticks());

    if (task_count <= 1)
    {
        return;
    }

    int next = next_task_id(current_task);
    if (next == current_task)
    {
        return;
    }

    if (tasks[current_task].state == TASK_RUNNING)
    {
        tasks[current_task].state = TASK_READY;
    }

    task_switch_to(next);
}

// Blocks the calling task for at least `ms` milliseconds
void task_sleep(uint32_t ms)
{
    uint32_t freq = pit_get_frequency();
    if (freq == 0)
    {
        freq = 100; // fallback; pit_init always runs before any task exists
    }

    uint32_t ticks = (ms * freq) / 1000;
    if (ticks == 0)
    {
        ticks = 1;
    }

    __asm__ volatile("cli");

    tasks[current_task].wake_tick = pit_get_ticks() + ticks;
    tasks[current_task].state = TASK_SLEEPING;

    int next = next_task_id(current_task);
    task_switch_to(next);

    __asm__ volatile("sti"); // re-enable interrupts on resume, no iret will do it for us
}

// Blocks the calling task until the target task exits (becomes UNUSED)
void task_wait(int target_id)
{
    while (tasks[target_id].state != TASK_UNUSED)
    {
        __asm__ volatile("cli");

        if (tasks[current_task].state == TASK_RUNNING)
        {
            tasks[current_task].state = TASK_READY;
        }

        int next = next_task_id(current_task);
        task_switch_to(next);

        __asm__ volatile("sti");
    }
}