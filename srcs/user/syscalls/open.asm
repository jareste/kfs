%define syscall int 0x30

global open
open:
    push ebp
    mov ebp, esp

    mov ebx, [ebp + 8]
    mov ecx, [ebp + 12]
    mov eax, 5

    syscall

    pop ebp
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
