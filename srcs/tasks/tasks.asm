[bits 32]
global fork_trampoline
fork_trampoline:
    mov eax, 0
    ret

global capture_cpu_state
capture_cpu_state:
    ; Save old task's registers
    mov eax, [esp + 4]   ; old_task pointer
    mov [eax + 0],  ebx
    mov [eax + 4],  ecx
    mov [eax + 8],  edx
    mov [eax + 12], esi
    mov [eax + 16], edi
    mov [eax + 20], ebp
    mov [eax + 24], esp
    pushfd                 ; get the old EFLAGS into stack
    pop dword [eax + 28]        ; store them in old_task->cpu.eflags (for example)

    ; Return to new task's EIP (stored on its stack)
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
