// Exercises malloc/free: allocates several blocks, writes a distinct pattern into each, frees a
// middle one and re-allocates a smaller size to prove the free list is reused (not just leaked
// forward), then verifies every still-live block still holds its own untouched data — proving
// the allocator isn't handing out overlapping memory.

#include "libc.h"

// Prints label followed by OK or FAIL depending on ok
static void report(const char *label, int ok)
{
    write(label, strlen(label));
    const char *msg = ok ? "OK\n" : "FAIL\n";
    write(msg, strlen(msg));
}

void _start(void)
{
    char *a = (char *)malloc(64);
    char *b = (char *)malloc(128);
    char *c = (char *)malloc(64);

    report("malloc a: ", a != 0);
    report("malloc b: ", b != 0);
    report("malloc c: ", c != 0);

    if (a == 0 || b == 0 || c == 0)
    {
        exit();
    }

    memset(a, 'A', 64);
    memset(b, 'B', 128);
    memset(c, 'C', 64);

    free(b); // frees the middle block — the allocator should be able to reuse this space

    char *d = (char *)malloc(32); // smaller than b, should land inside b's freed space
    report("malloc d (reuse freed space): ", d != 0);

    if (d != 0)
    {
        memset(d, 'D', 32);
    }

    int a_intact = 1, c_intact = 1;
    for (int i = 0; i < 64; i++)
    {
        if (a[i] != 'A')
        {
            a_intact = 0;
        }
        if (c[i] != 'C')
        {
            c_intact = 0;
        }
    }
    report("a still intact after b freed/d allocated: ", a_intact);
    report("c still intact after b freed/d allocated: ", c_intact);

    int d_intact = 1;
    if (d != 0)
    {
        for (int i = 0; i < 32; i++)
        {
            if (d[i] != 'D')
            {
                d_intact = 0;
            }
        }
    }
    report("d intact: ", d_intact);

    // d should be at or before where b used to start — proof it actually reused freed space
    // rather than just being handed fresh memory further up the heap
    report("d reused b's freed space (d <= old b address): ", d != 0 && d <= b);

    free(a);
    free(c);
    free(d);

    // After freeing everything, a big allocation should succeed again — proves free() actually
    // returns space to the list rather than leaking it
    char *big = (char *)malloc(60000);
    report("malloc big after freeing everything: ", big != 0);

    exit();

    for (;;)
    {
    }
}
