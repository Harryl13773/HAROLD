bits 32

section .text
global switch_task
global task_launch

extern task_exit  ; C function — the safety net if a task's entry function ever returns

; switch_task(uint32_t *old_esp, uint32_t new_esp) — saves callee-saved regs, then resumes the next task
switch_task:
    push ebp
    push ebx
    push esi
    push edi

    mov eax, [esp + 20]   ; 1st argument: where to store the outgoing task's esp
    mov [eax], esp

    mov eax, [esp + 24]   ; 2nd argument: the incoming task's saved esp
    mov esp, eax

    pop edi
    pop esi
    pop ebx
    pop ebp
    ret                    ; jumps to whatever return address sits on the new stack

; First-run trampoline for a brand-new task
task_launch:
    sti             ; a new task inherits interrupts-disabled — re-enable them
    pop eax
    call eax
    call task_exit