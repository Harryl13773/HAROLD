// Public interface for the userspace libc: syscall wrappers and string/memory helpers

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

// Blocks until a client connects, then returns a socket descriptor for it
int socket_accept(void);

// Blocks until data arrives or the connection closes; returns byte count, or 0 at a clean close
int socket_recv(int sockfd, char *buf, unsigned int max_len);

// Sends len bytes on an open socket; returns bytes sent, or -1
int socket_send(int sockfd, const char *buf, unsigned int len);

// Initiates a clean close of the socket; returns 0, or -1
int socket_close(int sockfd);

#define FAT_OPEN_CREATE 0   // fails if the file already exists
#define FAT_OPEN_TRUNCATE 1 // creates if missing, or empties an existing file before writing
#define FAT_OPEN_APPEND 2   // creates if missing, or resumes writing from the end of an existing file

// Creates filename, or opens an existing one per mode (FAT_OPEN_*); returns a fd or -1
int open_write(const char *filename, int mode);

// Writes len bytes from buf to fd (opened via open_write); returns bytes written, or -1
int fwrite(int fd, const char *buf, unsigned int len);

// Terminates the calling program
void exit(void);

unsigned int strlen(const char *s);
void *memset(void *dest, int value, unsigned int len);
void *memcpy(void *dest, const void *src, unsigned int len);
int strcmp(const char *a, const char *b);

// Converts value to a decimal string in buf (must be at least 12 bytes), returns the length
int itoa(int value, char *buf);

// Allocates at least `size` bytes from this process's private static heap, or 0 (NULL) if nothing
// big enough is free. Same first-fit/split/coalesce design as the kernel's own kmalloc.
void *malloc(unsigned int size);

// Returns a previously malloc'd block back to the free list
void free(void *ptr);

#endif