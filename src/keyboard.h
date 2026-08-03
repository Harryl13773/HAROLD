// Public interface for the PS/2 keyboard driver

#ifndef KEYBOARD_H
#define KEYBOARD_H

void keyboard_install(void);

// Blocks until a key is typed, returns its ASCII value
char keyboard_read_char(void);

#endif