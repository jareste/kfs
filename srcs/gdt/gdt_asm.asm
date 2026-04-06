global gdt_flush

gdt_flush:
    mov ax, 0x10        ; Kernel data segment selector
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    mov ax, 0x10        ; Kernel stack segment selector
    mov ss, ax

    jmp 0x08:reload_cs  ; Jump to the next instruction, far jump

reload_cs:
    ret                 ; Return to kernel_main

; File: tss.asm
global load_tss

load_tss:
    mov ax, 0x38  ; 0x38 is the offset of the TSS entry in the GDT (7th entry, 0x38 = 7 * 8)
    ltr ax        ; Load the TSS
    ret           ; Return to the caller

section .note.GNU-stack noalloc noexec nowrite progbits
