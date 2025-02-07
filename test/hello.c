void _start() {
    const char msg[] = "Hello World\n";
    asm volatile (
        "mov $1, %%rax\n"
        "mov $1, %%rdi\n"
        "mov %0, %%rsi\n"
        "mov $12, %%rdx\n"
        "syscall\n"
        "mov $60, %%rax\n"
        "xor %%rdi, %%rdi\n"
        "syscall\n"
        :: "r"(msg) : "rax", "rdi", "rsi", "rdx"
    );
}