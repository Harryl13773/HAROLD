bits 32

section .text

extern fault_handler          ; C function that handles all exceptions

; Stub for exceptions with no CPU-pushed error code — pushes a dummy 0
%macro ISR_NOERRCODE 1
global isr%1
isr%1:
    push dword 0
    push dword %1
    jmp isr_common_stub
%endmacro

; Stub for exceptions where the CPU pushes its own error code
%macro ISR_ERRCODE 1
global isr%1
isr%1:
    push dword %1
    jmp isr_common_stub
%endmacro

; Generates isr0 through isr31, using the correct macro per exception
ISR_NOERRCODE 0
ISR_NOERRCODE 1
ISR_NOERRCODE 2
ISR_NOERRCODE 3
ISR_NOERRCODE 4
ISR_NOERRCODE 5
ISR_NOERRCODE 6
ISR_NOERRCODE 7
ISR_ERRCODE   8
ISR_NOERRCODE 9
ISR_ERRCODE   10
ISR_ERRCODE   11
ISR_ERRCODE   12
ISR_ERRCODE   13
ISR_ERRCODE   14
ISR_NOERRCODE 15
ISR_NOERRCODE 16
ISR_NOERRCODE 17
ISR_NOERRCODE 18
ISR_NOERRCODE 19
ISR_NOERRCODE 20
ISR_NOERRCODE 21
ISR_NOERRCODE 22
ISR_NOERRCODE 23
ISR_NOERRCODE 24
ISR_NOERRCODE 25
ISR_NOERRCODE 26
ISR_NOERRCODE 27
ISR_NOERRCODE 28
ISR_NOERRCODE 29
ISR_NOERRCODE 30
ISR_NOERRCODE 31

; Shared landing point for all 32 stubs
isr_common_stub:
    pusha                      ; save all general-purpose registers

    mov ax, ds                 ; save the current data segment
    push eax

    mov ax, 0x10                ; switch to the kernel data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push esp                    ; pass a pointer to the saved registers
    call fault_handler
    add esp, 4                  ; clean up that argument

    pop eax                      ; restore the original data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    popa                         ; restore general-purpose registers
    add esp, 8                   ; discard error code + interrupt number
    iret                         ; return from interrupt