    .global _start
    _start:
        mov    $90, %eax        /* sys_write (4) */
        mov    $1, %ebx        /* stdout (1) */
        lea    msg, %ecx       /* Address of the message */
        mov    $msglen, %edx   /* Length of the message */
        int    $0x30           /* Make the system call */
    
        mov    $1, %eax        /* sys_exit (1) */
        xor    %ebx, %ebx      /* Exit code 0 */
        int    $0x30           /* Make the system call */
    
        .section .rodata
    msg:
        .ascii "Hello from stub!\n"
    msglen = . - msg