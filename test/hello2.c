#include <unistd.h>
#include <stdlib.h>
#include <string.h>

void main(int argc, char *argv[])
{

    write(1, "Argc count: ", 11);
    char chargc = '0' + argc;
    char argc_str[2] = {chargc, '\0'};
    write(1, argc_str, strlen(argc_str));
    write(1, "\n", 1);
    if (argc > 0)
    {
        int i = 0;
        write(1, "ArgvName: ", 9);
        write(1, argv[0], strlen(argv[0]));
    }
    write(1, "\n", 1);
    if (argc > 1)
    {
        write(1, "\nArgv1: ", 8);
        write(1, argv[1], strlen(argv[1]));
    }
    write(1, "\n", 1);

    const char msg[] = "Hello2222 World\n";
    write(1, msg, sizeof(msg) - 1);
    // exit(0);
    int pid = 0;
    pid = fork();
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
    exit(0);
}
