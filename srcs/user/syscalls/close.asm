%define syscall int 0x30

global close
close:
    push ebp
    mov ebp, esp

    mov ebx, [ebp + 8]
    mov eax, 6

    syscall

    pop ebp
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
