#ifndef TASK_H
#define TASK_H

#include <stdint.h>

#define MAX_TASKS 8
#define TASK_STACK_SIZE 4096

typedef enum
{
    TASK_UNUSED,
    TASK_READY,
    TASK_RUNNING,
    TASK_SLEEPING
} task_state_t;

// Everything needed to pause a task and later resume it
struct task
{
    uint32_t esp;
    uint32_t *stack_base;
    task_state_t state;
    int id;
    uint32_t wake_tick; // tick at which a sleeping task should wake
};

// Sets up the task system and turns the currently running code into task 0
void tasking_init(void);

// Creates a new task that will start executing at entry_point
int task_create(void (*entry_point)(void));

// Switches execution to a specific task by ID
void task_switch_to(int id);

// Called on every timer tick — wakes sleepers, then advances to the next task
void schedule(void);

// Ends the current task and switches to the next runnable one
void task_exit(void);

// Frees stack memory for any task that has exited
void task_reap(void);

// Blocks the calling task for at least `ms` milliseconds
void task_sleep(uint32_t ms);

// Blocks the calling task until the target task exits
void task_wait(int target_id);

#endif