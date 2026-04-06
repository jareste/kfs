%define syscall int 0x30

global exit
exit:
    push ebp
    mov ebp, esp

    mov ebx, [ebp + 8]
    mov eax, 1

    syscall

    pop ebp
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
