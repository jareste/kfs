#include "../uspace/stdlib/kfs_stdlib.h"
void _start()
{
    const char msg[] = "Hello2222 World\n";
    write(1, msg, sizeof(msg) - 1);
    int pid = fork();
    // int pid = 1;
    if (pid == 0)
    {
        /* Child process */
        const char child_msg[] = "Hello from child process!\n";
        write(1, child_msg, sizeof(child_msg) - 1);
        execve("/hello", NULL, NULL);
    }
    else if (pid > 0)
    {
        /* Parent process */
        const char parent_msg[] = "Hello from parent process!\n";
        write(1, parent_msg, sizeof(parent_msg) - 1);
    }
    else
    {
        /* Fork failed */
        const char error_msg[] = "Fork failed!\n";
        write(1, error_msg, sizeof(error_msg) - 1);
    }
    get_pid();
    exit(0);
}
