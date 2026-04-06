#include "../uspace/stdlib/kfs_stdlib.h"
void _start() {
    const char msg[] = "Hello2222 World\n";
    write(1, msg, sizeof(msg) - 1);
    get_pid();
    exit(0);
}