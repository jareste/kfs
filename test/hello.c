void main() {
    const char msg[] = "Hello World\n";
    asm volatile (
        "movl $4, %%eax\n"     /* sys_write (i386) */
        "movl $1, %%ebx\n"     /* stdout */
        "movl %0, %%ecx\n"     /* buf */
        "movl $12, %%edx\n"    /* len */
        "int $0x30\n"
        :: "r"(msg)
        : "eax","ebx","ecx","edx"
    );
}