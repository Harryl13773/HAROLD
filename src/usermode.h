// Public interface for entering ring 3 with a fresh user stack.

#ifndef USERMODE_H
#define USERMODE_H

// Maximum number of arguments placed on the user stack
#define MAX_ARGS 16

// Fixed virtual address for the ring-3 stack near the 4MB boundary
#define USER_STACK_VADDR 0x3FF000

// Builds a user stack with argc/argv and enters ring 3 at entry
void usermode_enter(void (*entry)(void), int argc, char *const argv[]);

#endif
