#ifndef TASK_H
#define TASK_H

#include <stdint.h>

#define MAX_TASKS 8
#define TASK_STACK_SIZE 4096

typedef enum
{
    TASK_UNUSED,
    TASK_READY,
    TASK_RUNNING
} task_state_t;

// Everything needed to pause a task and later resume it exactly where it left off
struct task
{
    uint32_t esp;
    uint32_t *stack_base;
    task_state_t state;
    int id;
};

// Sets up the task system and turns the currently running code into task 0
void tasking_init(void);

// Creates a new task that will start executing at entry_point
int task_create(void (*entry_point)(void));

// Switches execution to a specific task by ID
void task_switch_to(int id);

// Called on every timer tick — advances to the next runnable task
void schedule(void);

#endif