// Public interface for the merged console input source (keyboard + serial)

#ifndef CONSOLE_H
#define CONSOLE_H

// Blocks until a character is available from either the keyboard or the serial port (COM1),
// whichever comes first — the shared input source for anything that wants interactive stdin
// (the shell's own command line, and userspace programs via the read() syscall on fd 0). Useful
// for scripted testing over the serial line, which doesn't have PS/2 scancode/shift-key mapping
// to fight (sending an uppercase letter or a literal '/' over serial just works).
char console_read_char(void);

#endif
