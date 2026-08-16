// Public interface for the userspace libc: syscall wrappers and string/memory helpers.

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

// Gets the index-th directory entry; returns its size or -1 past the end.
int listdir(const char *dir_path, int index, char *name_buf, unsigned int buf_size, int *is_dir_out);

// Waits for a client and returns its socket descriptor.
int socket_accept(void);

// Receives data; returns bytes read or 0 if closed.
int socket_recv(int sockfd, char *buf, unsigned int max_len);

// Sends data; returns bytes sent or -1.
int socket_send(int sockfd, const char *buf, unsigned int len);

// Closes a socket; returns 0 or -1.
int socket_close(int sockfd);

#define FAT_OPEN_CREATE 0   // fail if file exists
#define FAT_OPEN_TRUNCATE 1 // create or truncate
#define FAT_OPEN_APPEND 2   // create or append
#define FAT_OPEN_MODIFY 3   // create or open for in-place edits

// Creates or opens a file for writing; returns its fd or -1.
int open_write(const char *filename, int mode);

// Writes len bytes to fd; returns bytes written or -1.
int fwrite(int fd, const char *buf, unsigned int len);

// Seeks to an absolute file offset; returns 0 or -1.
int seek(int fd, unsigned int offset);

// Creates an empty subdirectory; returns 0 or -1.
int mkdir(const char *path);

// Resolves a hostname to IPv4; returns 0 or -1.
int dns_resolve(const char *hostname, unsigned char out_ip[4]);

// Wall-clock time read from the CMOS RTC.
struct rtc_time
{
    unsigned short year;  // 4-digit year
    unsigned char month;  // 1-12
    unsigned char day;    // 1-31
    unsigned char hour;   // 0-23
    unsigned char minute; // 0-59
    unsigned char second; // 0-59
};

// Reads the current RTC date/time.
void rtc_read(struct rtc_time *out);

// Decoded PS/2 mouse packet; positive dy means up.
struct mouse_packet
{
    int dx;
    int dy;
    int left_button;
    int right_button;
    int middle_button;
};

// Waits for and returns the next mouse packet.
void mouse_read(struct mouse_packet *out);

// Runs argv[0] as an ELF and waits for it to exit; returns 0 or -1.
int run(int argc, char *const argv[]);

// Draws positioned VGA text with a raw attribute.
void draw_text(int row, int col, const char *text, unsigned int len, unsigned char attr);

// Terminates the calling program.
void exit(void);

unsigned int strlen(const char *s);
void *memset(void *dest, int value, unsigned int len);
void *memcpy(void *dest, const void *src, unsigned int len);
int strcmp(const char *a, const char *b);

// Converts an integer to decimal; returns the string length.
int itoa(int value, char *buf);

// Parses a leading decimal integer; returns 0 if invalid.
int atoi(const char *s);

// Allocates from the process's private heap, or returns NULL.
void *malloc(unsigned int size);

// Returns a previously malloc'd block back to the free list
void free(void *ptr);

#endif