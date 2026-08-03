// Allocates a user-mode stack and performs the ring0->ring3 jump into a task's entry point

#include <stdint.h>
#include "heap.h"
#include "usermode.h"

#define USER_STACK_SIZE 4096

extern void jump_to_usermode(uint32_t entry_eip, uint32_t user_esp);

void usermode_enter(void (*entry)(void))
{
    // Leaked by design for now — no process teardown exists yet to free it against
    uint8_t *stack = (uint8_t *)kmalloc(USER_STACK_SIZE);
    if (stack == NULL)
    {
        return;
    }

    uint32_t user_esp = (uint32_t)(stack + USER_STACK_SIZE);
    jump_to_usermode((uint32_t)entry, user_esp);
}