#include <stdint.h>
#include <stddef.h>
#include "task.h"
#include "heap.h"
#include "pit.h"
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

// Switches execution to a specific task by ID
void task_switch_to(int id)
{
    int old = current_task;
    current_task = id;
    tasks[id].state = TASK_RUNNING;
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

// Frees stack memory for any task that has exited
void task_reap(void)
{
    for (int i = 1; i < task_count; i++)
    {
        if (tasks[i].state == TASK_UNUSED && tasks[i].stack_base != NULL)
        {
            kfree(tasks[i].stack_base);
            tasks[i].stack_base = NULL;
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

    current_task = 0;
    task_count = 1;

    terminal_writestring("Tasking initialized\n");
}

// Hand-builds a new task's stack so its first resume jumps into entry_point
int task_create(void (*entry_point)(void))
{
    if (task_count >= MAX_TASKS)
    {
        return -1;
    }

    int id = task_count;
    struct task *t = &tasks[id];

    t->stack_base = (uint32_t *)kmalloc(TASK_STACK_SIZE);
    if (t->stack_base == NULL)
    {
        return -1;
    }

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

    task_count++;
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