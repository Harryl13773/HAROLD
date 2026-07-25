#ifndef LIBC_H
#define LIBC_H

// Writes len bytes from buf to standard output, returns bytes written
int write(const char *buf, unsigned int len);

// Reads up to len bytes from fd into buf (fd 0 = stdin/keyboard, else a file); returns bytes read, 0 at EOF, or -1
int read(int fd, char *buf, unsigned int len);

// Opens filename for reading, returns a file descriptor (>= 3) or -1
int open(const char *filename);

// Closes a descriptor opened by open(); returns 0 or -1
int close(int fd);

// Gets the index-th file's name (into name_buf) and returns its size, or -1 past the last file
int listdir(int index, char *name_buf, unsigned int buf_size);

// Terminates the calling program
void exit(void);

unsigned int strlen(const char *s);
void *memset(void *dest, int value, unsigned int len);
void *memcpy(void *dest, const void *src, unsigned int len);
int strcmp(const char *a, const char *b);

// Converts value to a decimal string in buf (must be at least 12 bytes), returns the length
int itoa(int value, char *buf);

#endif