#ifndef STDLIB_H
#define STDLIB_H
#include "../../srcs/utils/stdint.h" /* Take same stdint as kernel has to avoid conflicts. */

#ifndef NULL
#define NULL (void*)0
#endif

typedef void (*signal_handler_t)(int);

int write(int fd, const char* buf, size_t count);
int kill(uint32_t pid, uint32_t signal);
int signal(int signal, signal_handler_t handler);
size_t read(int fd, char* buf, size_t count);
int get_pid();
void exit(int status);
int open(const char* path, int flags);
int close(int fd);
int fork();
void sleep(uint32_t seconds);
void usleep(uint32_t microseconds);
time_t time(time_t* tloc);
int execve(const char* path, char* const argv[], char* const envp[]);

#endif
