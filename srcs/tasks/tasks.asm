[bits 32]
global switch_context

switch_context:
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

    ; Restore new task's registers
    mov eax, [esp + 8]   ; new_task pointer
    mov ebx, [eax + 0]
    mov ecx, [eax + 4]
    mov edx, [eax + 8]  
    mov esi, [eax + 12]
    mov edi, [eax + 16]
    mov ebp, [eax + 20]
    mov esp, [eax + 24]
    push dword [eax + 28]        ; restore new_task->cpu.eflags
    popfd

    ; .switch_to_new_task:
    ; jmp .switch_to_new_task

    ; Return to new task's EIP (stored on its stack)
    ret

global switch_context_to_user

switch_context_to_user:
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

    ; Restore new task's registers
    mov eax, [esp + 8]   ; new_task pointer
    mov ebx, [eax + 0]
    mov ecx, [eax + 4]
    mov edx, [eax + 8]  
    mov esi, [eax + 12]
    mov edi, [eax + 16]
    mov ebp, [eax + 20]
    mov esp, [eax + 24]
    push dword [eax + 28]        ; restore new_task->cpu.eflags
    popfd

    ; .switch_to_new_task:
    ; jmp .switch_to_new_task

    ; Return to new task's EIP (stored on its stack)
    ret


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
