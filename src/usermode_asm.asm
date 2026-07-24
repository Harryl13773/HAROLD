bits 32

section .text
global jump_to_usermode

; void jump_to_usermode(uint32_t entry_eip, uint32_t user_esp)
jump_to_usermode:
    mov ebx, [esp + 4]   ; entry_eip
    mov ecx, [esp + 8]   ; user_esp

    mov ax, 0x23          ; user data selector (0x20 | RPL 3) — legal to load pre-iret since max(CPL,RPL)<=DPL
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push dword 0x23        ; SS
    push ecx                ; ESP
    pushfd
    pop eax
    or eax, 0x200            ; force IF on — ring 3 code should run preemptible, like any other task
    push eax                  ; EFLAGS
    push dword 0x1B            ; CS (user code selector, 0x18 | RPL 3)
    push ebx                    ; EIP
    iret