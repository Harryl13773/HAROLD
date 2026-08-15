// Public interface for loading and running static ELF32 executables

#ifndef ELF_H
#define ELF_H

// Loads a static ELF32 executable from the FAT volume and runs it in ring 3, passing argc/argv
// through to it; returns -1 on failure, never returns on success. argv[0] is expected to be filename.
int elf_load_and_run(const char *filename, int argc, char *const argv[]);

#endif