#ifndef SYSCALLS_H
#define SYSCALLS_H
#include "../keyboard/idt.h"
#include "../keyboard/signals.h"
#include "../time/time.h"

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
    // ...
    SYS_UNAME = 122,
    SYS_MPROTECT = 125,
    SYS_INIT_MODULE = 128,
    SYS_DELETE_MODULE = 129,
    SYS_GET_KERNEL_SYMS = 130,
    SYS_QUERY_MODULE = 131,
    SYS_QUOTACTL = 132,
    SYS_WRITEV = 146,
    SYS_GETTIMEOFDAY = 169,
    SYS_NANOSLEEP = 170,
    SYS_SLEEP = 171,
    SYS_RT_SIGACTION = 174,
    SYS_SIGPROCMASK = 175,
    SYS_GETCWD = 183,
    SYS_UGETRLIMIT = 191,
    SYS_MMAP2 = 192,
    SYS_GETUID32 = 199,
    SYS_GETGID32 = 200,
    SYS_GETEUID32 = 201,
    SYS_SETUID32 = 213,
    SYS_SETGID32 = 214,
    SYS_GETRUSAGE = 217,
    SYS_FADVISE64 = 221,
    SYS_SET_THREAD_AREA = 243,
    SYS_EXIT_GROUP = 252,
    SYS_SET_TID_ADDRESS = 258,
    SYS_READLINKAT = 305,
    SYS_SET_ROBUST_LIST = 311,
    SYS_GETRANDOM = 355,
    SYS_STATX = 383,
    SYS_RSEQ = 386,
    SYS_MAX_SYSCALL = 404
} syscalls_num;

typedef struct __attribute__((packed))
{
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t esp;
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;
    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;
    uint32_t esp_user;
    uint32_t ss_user;
}  iret_regs_t;

int syscall_handler(iret_regs_t* reg);

void init_syscalls();

int sys_get_pid();
int sys_kill(uint32_t pid, uint32_t signal);
int sys_signal(uint32_t pid, signal_handler_t hand);
void sys_sleep(uint32_t seconds);
time_t sys_time(time_t* tloc);

#endif // SYSCALLS_H
