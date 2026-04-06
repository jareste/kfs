BITS 32

section .multiboot
align 4
    dd 0x1BADB002 ; Multiboot magic
    dd 0x00       ; Flags
    dd -(0x1BADB002 + 0x00) ; Checksum

section .text
global start
extern kernel_main

start:
    cli
    mov esp, 0x90000

    push ebx ; Push multiboot info pointer
    push eax ; Push multiboot magic number
    call kernel_main

    cli
.hang:
    hlt ; Halt forever should not happen as main has already an infinite loop, but just in case
    jmp .hang

section .note.GNU-stack noalloc noexec nowrite progbits
