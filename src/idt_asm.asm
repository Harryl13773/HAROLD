bits 32

section .text
global idt_load

; Loads the IDT pointer passed in from C
idt_load:
    mov eax, [esp + 4]
    lidt [eax]
    ret