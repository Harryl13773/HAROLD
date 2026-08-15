// Public interface for the FAT16 filesystem driver

#ifndef FAT_H
#define FAT_H

#include <stdint.h>

// Reads the boot sector on the primary master and validates it's FAT16
int fat_init(void);

// Looks up filename (any length, via LFN when present) and copies it into buffer
int fat_read_file(const char *filename, uint8_t *buffer, uint32_t buffer_size);

// Opens filename for reading, returns a file descriptor (>= 3) or -1
int fat_open(const char *filename);

// Reads up to len bytes from fd, resuming from the last position; returns bytes read, 0 at EOF, or -1
int fat_read(int fd, uint8_t *buffer, uint32_t len);

// Closes a descriptor opened by fat_open or fat_open_write; returns 0 or -1 on an invalid fd
int fat_close(int fd);

// Closes every descriptor still owned by task_id — called by task_reap on task exit
void fat_close_all_for_task(int task_id);

#define FAT_OPEN_CREATE 0   // fails if the file already exists
#define FAT_OPEN_TRUNCATE 1 // creates if missing, or empties an existing file before writing
#define FAT_OPEN_APPEND 2   // creates if missing, or resumes writing from the end of an existing file

// Creates filename, or opens an existing one per mode (FAT_OPEN_*); returns a fd or -1
int fat_open_write(const char *filename, int mode);

// Writes len bytes from buffer to fd (opened via fat_open_write), extending the file as needed;
// returns bytes actually written (less than len only if the disk fills up)
int fat_write(int fd, const uint8_t *buffer, uint32_t len);

// Gets the index-th root directory entry's name and size; returns 0, or -1 past the last entry
int fat_list_entry(int index, char *name_out, uint32_t name_out_size, uint32_t *size_out);

#endif