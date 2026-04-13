[bits 32]
; Adjust this with values provideds by __offsets.c file.
%define TASK_ESP_OFFSET          36
%define TASK_KERNEL_STACK_OFFSET 44
%define TASK_ENV_OFFSET          60
%define TASK_PAGE_DIR_OFFSET     56

extern current_task
extern get_next_task
extern tss_set_stack
extern set_active_env
extern vmm_switch_directory
extern vmm_set_kernel_dir
extern free_finished_tasks

global schedule
schedule:
    pushfd
    pusha

    mov eax, [current_task]
    test eax, eax
    jz .restore_and_ret

    mov [eax + TASK_ESP_OFFSET], esp

    call free_finished_tasks

    call get_next_task
    test eax, eax
    jz .restore_and_ret

    cmp eax, [current_task]
    je .restore_and_ret

    ; Guardar next en ESI (callee-saved)
    mov esi, eax

    ; tss_set_stack(next->kernel_stack)
    push dword [esi + TASK_KERNEL_STACK_OFFSET]
    call tss_set_stack
    add esp, 4

    ; set_active_env(next->env)
    push dword [esi + TASK_ENV_OFFSET]
    call set_active_env
    add esp, 4

    ; vmm_switch_directory(next->page_dir) or vmm_set_kernel_dir()
    mov ecx, [esi + TASK_PAGE_DIR_OFFSET]
    test ecx, ecx
    jz .use_kernel_dir

    push ecx
    call vmm_switch_directory
    add esp, 4
    jmp .switch_stack

.use_kernel_dir:
    call vmm_set_kernel_dir

.switch_stack:
    mov [current_task], esi
    mov esp, [esi + TASK_ESP_OFFSET]

    popa
    popfd
    ret

.restore_and_ret:
    popa
    popfd
    ret
    

section .note.GNU-stack noalloc noexec nowrite progbits
