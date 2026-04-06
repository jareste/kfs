%define syscall int 0x30

global fork
fork:
    push ebp
    mov ebp, esp

    mov eax, 2

    syscall

    pop ebp
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
