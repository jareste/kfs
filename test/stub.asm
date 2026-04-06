; stub.asm — a tiny ELF that does write(1, msg, len) then exit(0)
bits 32

section .data
msg:    db  "Hello from stub!", 10
msglen: equ $ - msg

section .text
global _start
_start:
    ; ssize_t write(int fd, const void *buf, size_t count)
    mov eax, 4        ; SYS_write
    mov ebx, 1        ; fd = stdout
    mov ecx, msg      ; buf
    mov edx, msglen   ; count
    int 0x30

    ; void exit(int status)
    mov eax, 1        ; SYS_exit
    xor ebx, ebx      ; status = 0
    int 0x30
