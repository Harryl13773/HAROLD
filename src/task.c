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
            // The whole sequence below must be atomic: without this, task_create could see this
            // slot as reusable partway through (stack_base already NULL, page_directory not yet)
            // and start a brand new task here before this cleanup finishes — exactly the race that
            // was corrupting page directories and causing the intermittent faults
            uint32_t flags = save_and_disable_interrupts();

            kfree(tasks[i].stack_base);
            tasks[i].stack_base = NULL;

            fat_close_all_for_task(i); // a task that exited without calling close() itself, cleaned up here instead

            if (tasks[i].page_directory != NULL)
            {
                paging_free_user_directory(tasks[i].page_directory); // private tables/data frames first
                pmm_free_frame((uint32_t)tasks[i].page_directory);   // then the directory frame itself
                tasks[i].page_directory = NULL;
            }

            restore_interrupts(flags);
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
    // Atomic for the same reason task_reap() is: a half-built task here (stack_base already
    // non-NULL, state still TASK_UNUSED because it isn't set to TASK_READY until the very end)
    // matches task_reap()'s own reap condition exactly. Without this, a timer interrupt landing
    // between kmalloc(stack) and state = TASK_READY lets task_reap() free the brand-new stack out
    // from under this function, which then keeps building on the now-NULL stack_base — a real,
    // reproduced bug: confirmed via a headless run (task_create's own trace showed task_reap
    // reaping the slot mid-construction, immediately followed by an Invalid Opcode fault).
    uint32_t flags = save_and_disable_interrupts();

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
            restore_interrupts(flags);
            return -1;
        }
        id = task_count;
        task_count++;
    }

    struct task *t = &tasks[id];

    t->stack_base = (uint32_t *)kmalloc(TASK_STACK_SIZE);
    if (t->stack_base == NULL)
    {
        restore_interrupts(flags);
        return -1;
    }

    t->page_directory = paging_clone_kernel_directory();
    if (t->page_directory == NULL)
    {
        kfree(t->stack_base); // don't leak the stack we just allocated if the directory clone fails
        t->stack_base = NULL;
        restore_interrupts(flags);
        return -1;
    }

    terminal_writestring("Task ");
    terminal_print_dec((uint32_t)id);
    terminal_writestring(": cloned page directory at ");
    terminal_print_hex((uint32_t)t->page_directory);
    terminal_writestring("\n");

    uint32_t *stack_top = (uint32_t *)((uint8_t *)t->stack_base + TASK_STACK_SIZE);

    *(--stack_top) = (uint32_t)entry_point; // consumed by task_launch's pop
    *(--stack_top) = (uint32_t)task_launch; // consumed by switch_task's ret
    *(--stack_top) = 0;                     // fake saved ebp
    *(--stack_top) = 0;                     // fake saved ebx
    *(--stack_top) = 0;                     // fake saved esi
    *(--stack_top) = 0;                     // fake saved edi — becomes this task's esp

    t->esp = (uint32_t)stack_top;
    t->state = TASK_READY;
    t->id = id;

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