; the code that runs very first
bits 32

; this part never execute, it just loads to the kernel
section .multiboot
align 4
    dd 0x1BADB002           ;mentioning to GRUB that this multi-boot complant kernel
    dd 0x00000003           ; flag field
    dd -(0x1BADB002 + 0x00000003)   ;check sum

; real entry point
section .text
global _start
extern kernel_main

_start:
    cli                     ; clearing interrupt flag
    mov esp, stack_top      ; setting up stack pointer
    call kernel_main        ; handing to the kernel_main.c

; the backup if kernel_main returns
.hang:
    hlt
    jmp .hang

; reserving stack space
section .bss
align 16
stack_bottom:
    resb 16384
stack_top: