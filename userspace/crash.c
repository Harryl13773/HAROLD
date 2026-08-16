// Write past mapped memory to guarantee a page fault.
#include "libc.h"

void _start(void)
{
    const char *msg = "crash: about to write to unmapped memory\n";
    write(msg, strlen(msg));

    volatile int *bad_ptr = (volatile int *)0x10000000; // 256MB — well past the 4MB identity-mapped region
    *bad_ptr = 42;

    const char *unreachable = "crash: survived the write (this should never print)\n";
    write(unreachable, strlen(unreachable));

    exit();

    for (;;)
    {
    }
}