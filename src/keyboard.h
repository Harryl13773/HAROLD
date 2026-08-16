// Public interface for the PS/2 keyboard driver.

#ifndef KEYBOARD_H
#define KEYBOARD_H

// Enables the keyboard IRQ handler and clears its input buffer
void keyboard_install(void);

// True if a key is waiting in the buffer without blocking
int keyboard_has_char(void);

// Blocks until a key is typed, returns its ASCII value
char keyboard_read_char(void);

#endif