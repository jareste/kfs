#ifndef SYSCALLS_H
#define SYSCALLS_H
#include "../keyboard/idt.h"

// #define SYS_EXIT    0
// #define SYS_WRITE   1
// #define SYS_READ    2
// #define SYS_OPEN    3
// #define SYS_CLOSE   4
// #define SYS_GETPID  5
// #define SYS_SLEEP   6
// #define SYS_KILL    7
// #define SYS_SIGNAL  8
// #define MAX_SYSCALLS 9

typedef enum {
    SYS_RESTART_SYSCALL = 0,
    SYS_EXIT = 1,
    SYS_FORK = 2,
    SYS_READ = 3,
    SYS_WRITE = 4,
    SYS_OPEN = 5,
    SYS_CLOSE = 6,
    SYS_WAITPID = 7,
    SYS_CREAT = 8,
    SYS_LINK = 9,
    SYS_UNLINK = 10,
    SYS_EXECVE = 11,
    SYS_CHDIR = 12,
    SYS_TIME = 13,
    SYS_MKNOD = 14,
    SYS_CHMOD = 15,
    SYS_LCHOWN = 16,
    SYS_BREAK = 17,
    SYS_OLDSTAT = 18,
    SYS_LSEEK = 19,
    SYS_GETPID = 20,
    SYS_MOUNT = 21,
    SYS_UMOUNT = 22,
    SYS_SETUID = 23,
    SYS_GETUID = 24,
    SYS_STIME = 25,
    SYS_PTRACE = 26,
    SYS_ALARM = 27,
    SYS_OLDFSTAT = 28,
    SYS_PAUSE = 29,
    SYS_UTIME = 30,
    SYS_STTY = 31,
    SYS_GTTY = 32,
    SYS_ACCESS = 33,
    SYS_NICE = 34,
    SYS_FTIME = 35,
    SYS_SYNC = 36,
    SYS_KILL = 37,
    SYS_RENAME = 38,
    SYS_MKDIR = 39,
    SYS_RMDIR = 40,
    SYS_DUP = 41,
    SYS_PIPE = 42,
    SYS_TIMES = 43,
    SYS_PROF = 44,
    SYS_BRK = 45,
    SYS_SETGID = 46,
    SYS_GETGID = 47,
    SYS_SIGNAL = 48,
    SYS_GETEUID = 49,
    SYS_GETEGID = 50,
    SYS_ACCT = 51,
    // ...
    SYS_LOCK = 53,
    SYS_IOCTL = 54,
    SYS_FCNTL = 55,
    SYS_MPX = 56,
    SYS_SETPGID = 57,
    SYS_ULIMIT = 58,
    SYS_OLDOLDUNAME = 59,
    SYS_UMASK = 60,
    SYS_CHROOT = 61,
    SYS_USTAT = 62,
    SYS_DUP2 = 63,
    SYS_GETPPID = 64,
    SYS_GETPGRP = 65,
    SYS_SETSID = 66,
    SYS_SIGACTION = 67,
    SYS_SIGGETMASK = 68,
    SYS_SIGSETMASK = 69,
    SYS_SIGSETREUID = 70,
    SYS_SIGSETREGID = 71,
    SYS_SIGSUSPEND = 72,
    SYS_SIGPENDING = 73,
    SYS_SETHOSTNAME = 74,
    SYS_SETRLIMIT = 75,
    SYS_GETRLIMIT = 78,
    SYS_SETTIMEOFDAY = 79,
    // ...
    SYS_REBOOT = 88,
    SYS_READDIR = 89,
    SYS_MMAP = 90,
    SYS_MUNMAP = 91,
    SYS_WAIT4 = 114,
    // ... CUSTOM SYSCALLS
    SYS_SCHED_YIELD = 158,
    // ...
    SYS_GETTIMEOFDAY = 169,
    SYS_NANOSLEEP = 170,
    SYS_SLEEP = 171,
    // SYS_TIME = 174,
    SYS_MAX_SYSCALL = 175,

} syscalls_num;

int syscall_handler(registers reg);

void init_syscalls();

#endif // SYSCALLS_H
