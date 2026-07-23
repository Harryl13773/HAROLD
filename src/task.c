#include <stdint.h>
#include <stddef.h>
#include "task.h"
#include "heap.h"
#include "terminal.h"

extern void switch_task(uint32_t *old_esp, uint32_t new_esp); // defined in task_asm.asm
extern void task_launch(void);                                // defined in task_asm.asm

static struct task tasks[MAX_TASKS];
static int current_task = -1;
static int task_count = 0;

// Called if a task's entry function ever returns — a task must never fall off the end
void task_exit(void)
{
    terminal_writestring("Task exited\n");
    for (;;)
    {
        __asm__ volatile("hlt");
    }
}

// Turns the currently executing code into task 0 — it's already running, nothing to construct
void tasking_init(void)
{
    for (int i = 0; i < MAX_TASKS; i++)
    {
        tasks[i].state = TASK_UNUSED;
    }

    tasks[0].id = 0;
    tasks[0].state = TASK_RUNNING;
    tasks[0].stack_base = NULL; // task 0 keeps using the kernel's original boot stack

    current_task = 0;
    task_count = 1;

    terminal_writestring("Tasking initialized\n");
}

// Hand-builds a new task's stack so its first resume jumps straight into entry_point
int task_create(void (*entry_point)(void))
{
    if (task_count >= MAX_TASKS)
    {
        return -1; // no free task slots
    }

    int id = task_count;
    struct task *t = &tasks[id];

    t->stack_base = (uint32_t *)kmalloc(TASK_STACK_SIZE);
    if (t->stack_base == NULL)
    {
        return -1; // heap couldn't provide a stack
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

// Switches execution to a specific task by ID — a preview of what the scheduler will call repeatedly
void task_switch_to(int id)
{
    int old = current_task;
    current_task = id;
    tasks[id].state = TASK_RUNNING;
    switch_task(&tasks[old].esp, tasks[id].esp);
}

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
    return from; // nothing else runnable — stay on the current task
}

// Called from the PIT handler every tick — the actual round-robin switch
void schedule(void)
{
    if (task_count <= 1)
    {
        return; // only one task exists, nothing to switch to
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