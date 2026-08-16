// Public interface for loading and running static ELF32 executables.

#ifndef ELF_H
#define ELF_H

// Loads a static ELF32 from FAT and runs it in ring 3 with argc/argv; returns -1 on failure
int elf_load_and_run(const char *filename, int argc, char *const argv[]);

#endif