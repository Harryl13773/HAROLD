// Public interface for letting a ring-3 program launch another ELF program.

#ifndef EXEC_H
#define EXEC_H

// Launches argv[0] as an ELF program and waits for it to exit; returns 0 or -1 on failure
int exec_run(int argc, char *const argv[]);

#endif
