#ifndef FAT_H
#define FAT_H

#include <stdint.h>

// Reads the boot sector on the primary master and validates it's FAT16
int fat_init(void);

// Looks up an 8.3 filename in the root directory and copies it into buffer
int fat_read_file(const char *filename, uint8_t *buffer, uint32_t buffer_size);

#endif