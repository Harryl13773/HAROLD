// Public interface for entering ring 3 with a fresh user stack

#ifndef USERMODE_H
#define USERMODE_H

// Max argv entries usermode_enter will place on the new stack (plenty for this project's shell)
#define MAX_ARGS 8

// Allocates a user stack, lays out a standard argc/argv call frame on it, and jumps entry into
// ring 3. A program that ignores argc/argv (still declared void _start(void)) is unaffected —
// the extra frame just sits above its real stack usage, unread.
void usermode_enter(void (*entry)(void), int argc, char *const argv[]);

#endif
