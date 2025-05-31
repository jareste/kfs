    section .data
        msg db "Hello from stub!", 0xA  ; Message with a newline
        msglen equ $ - msg              ; Length of the message
    
    section .text
    global _start
    
    _start:
        mov eax, 4          ; sys_write (4)
        mov ebx, 1          ; stdout (1)
        mov ecx, msg        ; Address of the message
        mov edx, msglen     ; Length of the message
        int 0x80            ; Perform the system call
    
        mov eax, 1          ; sys_exit (1)
        xor ebx, ebx        ; Exit code 0
        int 0x30            ; Perform the system call
        pop ebp