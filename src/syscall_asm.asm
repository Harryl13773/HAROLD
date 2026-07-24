bits 32

section .text
global syscall_stub
extern syscall_handler

;two placeholder dwords are pushed here anyway
syscall_stub:
    push dword 0
    push dword 0
    pusha

    mov ax, ds
    push eax

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push esp
    call syscall_handler
    add esp, 4

    pop eax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    popa
    add esp, 8
    iret