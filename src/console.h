// Public interface for the merged console input source (keyboard + serial).

#ifndef CONSOLE_H
#define CONSOLE_H

// Waits for input from the keyboard or serial port for interactive stdin
char console_read_char(void);

#endif
