// Public interface for entering ring 3 with a fresh user stack

#ifndef USERMODE_H
#define USERMODE_H

// Allocates a user stack and jumps entry into ring 3
void usermode_enter(void (*entry)(void));

#endif