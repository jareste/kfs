#ifndef EXECVE_H
#define EXECVE_H

int sys_execve(const char* path, char* const argv[], char* const envp[]);

#endif // EXECVE_H
