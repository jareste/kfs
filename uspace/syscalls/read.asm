%define syscall int 0x30

global read
read:
    push ebp
    mov ebp, esp

    mov ebx, [ebp + 8]
    mov ecx, [ebp + 12]
    mov edx, [ebp + 16]
    mov eax, 3

    syscall

    pop ebp
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
