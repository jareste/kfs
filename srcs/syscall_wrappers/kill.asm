%define syscall int 0x80

global kill
kill:
    push ebp
    mov ebp, esp

    mov ebx, [ebp + 8]
    mov ecx, [ebp + 12]
    mov eax, 7

    syscall

    pop ebp
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
