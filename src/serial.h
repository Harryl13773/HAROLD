// Public interface for the COM1 serial driver — a persistent, non-scrolling log independent of VGA

#ifndef SERIAL_H
#define SERIAL_H

// Initializes COM1 at 38400 baud, 8N1, with FIFO enabled, and wires up IRQ4-driven receive
void serial_init(void);

// Writes one character, blocking until the transmit holding register is empty
void serial_putchar(char c);

// Writes a full string via serial_putchar
void serial_writestring(const char *str);

// True if a received byte is waiting to be read without blocking
int serial_has_char(void);

// Blocks until a byte arrives on COM1, returns it (echoed back out already, by serial_handler)
char serial_read_char(void);

#endif
