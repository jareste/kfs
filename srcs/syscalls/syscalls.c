#include "syscalls.h"
#include "../utils/stdint.h"
#include "../display/display.h"
#include "../tasks/task.h"
#include "../tasks/elf.h"
#include "../keyboard/signals.h"
#include "../keyboard/keyboard.h"
#include "../time/time.h"
#include "../panic/kpanic.h"
#include "../gdt/gdt.h"
#include "../memory/kmalloc.h"

typedef int (*syscall_handler_6_t)(uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5, uint32_t arg6);
typedef int (*syscall_handler_5_t)(uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5);
typedef int (*syscall_handler_4_t)(uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);
typedef int (*syscall_handler_3_t)(uint32_t arg1, uint32_t arg2, uint32_t arg3);
typedef int (*syscall_handler_2_t)(uint32_t arg1, uint32_t arg2);
typedef int (*syscall_handler_1_t)(uint32_t arg1);
typedef int (*syscall_handler_0_t)();

bool syscall_happening = false;

typedef enum
{
    RET_INT = 0,
    RET_PTR = 1,
    RET_SIZE = 2,
    RET_VOID = 3,
} ret_value_size;

typedef union
{
    int int_value;
    void* ptr_value;
    size_t size_value;
} syscall_return_t;

typedef union
{
    void* handler;
} syscall_handler_t;

typedef struct
{
    syscall_return_t ret_value;
    syscall_handler_t handler;
    uint8_t num_args;
    ret_value_size ret_value_entry;
} syscall_entry_t;

struct user_desc
{
    unsigned int entry_number;
    unsigned int base_addr;
    unsigned int limit;
    unsigned int seg_32bit:1;
    unsigned int contents:2;
    unsigned int read_exec_only:1;
    unsigned int limit_in_pages:1;
    unsigned int seg_not_present:1;
    unsigned int useable:1;
};

syscall_entry_t syscall_table[SYS_MAX_SYSCALL];

int sys_set_thread_area(struct user_desc *u_info)
{
    if (u_info->entry_number == (unsigned)-1)
        u_info->entry_number = TLS_GDT_ENTRY;

    gdt_set_entry(TLS_GDT_ENTRY, u_info->base_addr, u_info->limit, 0xF2,
                  u_info->limit_in_pages ? 0xC0 : 0x40);
    register_gdt();

    get_current_task()->tls_base = u_info->base_addr;

    uint16_t sel = (TLS_GDT_ENTRY * 8) + 3;
    __asm__ volatile ("mov %0, %%gs" :: "r"(sel));
    return 0;
}

int sys_set_tid_address(int *tidptr)
{
    (void)tidptr;
    return get_current_task()->pid;
}

int sys_exit(int status)
{
    _exit(status);
    return status;
}

int _sys_write(int fd, const char* buf, size_t count)
{
    if (!buf || count == 0)
        return -1;

    if (fd < 0)
        return -1;

    return sys_write(fd, buf, count);
}

int _sys_read(int fd, char* buf, size_t count)
{
    if (!buf || count == 0)
        return -1;

    // return sys_read2(fd, buf, count);
    // if (fd == 0)
    // {
    //     char* buffer = get_line();
    //     size_t len = strlen(buffer);
    //     buffer[len] = '\0'; /* remove '\n' */
    //     strcpy(buf, buffer);
    //     return len;
    // }

    return sys_read(fd, buf, count);
}

int _sys_getdents64(int fd, void *dirp, size_t count)
{
    if (!dirp || count == 0)
        return -1;

    if (fd < 0)
        return -1;

    return sys_getdents64(fd, dirp, count);
}

int _sys_open(const char* path, int flags)
{
    // kprintf("Syscall: open(%s, %d)\n", path, flags);
    return sys_open(path, flags);
}

int _sys_close(int fd)
{
    // kprintf("Syscall: close(%d)\n", fd);
    return sys_close(fd, get_current_task());
}

int sys_get_pid()
{
    return get_current_task()->pid;
}

int sys_kill(uint32_t pid, uint32_t signal)
{
    return _kill(pid, signal);
}

int sys_signal(uint32_t pid, signal_handler_t hand)
{
    return _signal(pid, hand);
}

pid_t sys_fork(iret_regs_t* parent_frame)
{
    return _fork(parent_frame);
}

void sys_usleep(uint32_t microseconds)
{
    _usleep(microseconds);
}

void sys_sleep(uint32_t seconds)
{
    _sleep(seconds);
}

time_t sys_time(time_t* tloc)
{
    return _time(tloc);
}

int sys_execve(const char* path, char* const argv[], char* const envp[])
{
    int ret;
    char* _path;
    int status;

    if (strcmp(path, "/proc/self/exe") == 0)
        _path = kstrdup(get_current_task()->name);
    else
        _path = kstrdup(path);

    ret = exec_bin(_path, argv, envp);
    if (ret < 0)
    {
        kprintf("Failed to execute binary: %s\n", _path);
        kfree(_path);
        return -1;
    }
    kfree(_path);

    status = 0;
    _waitpid(ret, &status, 0);
    _exit(status); /* should never return */
    return 0;
}

pid_t sys_waitpid(pid_t pid, int *status, int options)
{
    return _waitpid(pid, status, options);
}

int sys_rt_sygprocmask(int how, const uint32_t *set, uint32_t *oldset)
{
    (void)how;
    (void)set;
    (void)oldset;
    return 0;
}

int sys_set_robust_list(void *head, size_t len)
{
    (void)head; (void)len;
    return 0;
}

int sys_ugetrlimit(int resource, void *rlim)
{
    uint32_t *r = (uint32_t*)rlim;
    (void)resource;
    r[0] = 8 * 1024 * 1024; // rlim_cur = 8MB
    r[1] = 0xFFFFFFFF;       // rlim_max = unlimited
    return 0;
}

int sys_getppid()
{
    task_t *t = get_current_task();
    return t->parent ? t->parent->pid : 1;
}

int sys_geteuid32()
{
    return get_current_task()->euid;
}

int sys_getgid32()
{
    return get_current_task()->gid;
}

int sys_setgid32(gid_t gid)
{
    task_t *t = get_current_task();
    t->gid = gid;
    return 0;
}

int sys_setuid32(uid_t uid)
{
    task_t *t = get_current_task();
    t->uid = uid;
    return 0;
}

int sys_getuid32()
{
    return get_current_task()->uid;
}

int sys_getrandom(void *buf, size_t buflen, unsigned int flags)
{
    (void)flags;
    memset(buf, 0x42, buflen);
    return buflen;
}

int sys_rt_sigaction(int sig, void *act, void *oldact, size_t sigsetsize)
{
    (void)sig; (void)act; (void)oldact; (void)sigsetsize;
    return 0;
}

/* no-op/succeed as musl/busybox tries to use it, but as we don't track a process name field, we just ignore it.
*/
int sys_prctl(int option, unsigned long arg2, unsigned long arg3, unsigned long arg4, unsigned long arg5)
{
#define PR_SET_NAME  15
#define PR_GET_NAME  16

    (void)arg3; (void)arg4; (void)arg5;
    switch (option)
    {
        case PR_SET_NAME:
            return 0;
        case PR_GET_NAME:
            if (arg2)
                memcpy((void*)arg2, get_current_task()->name, 16);
            return 0;
        default:
            return -38; // -ENOSYS
    }
}

int sys_readlinkat(int dirfd, const char *path, char *buf, size_t bufsiz)
{
    (void)dirfd;
    if (strcmp(path, "/proc/self/exe") == 0)
    {
        strncpy(buf, get_current_task()->name, bufsiz);
        return strlen(buf);
    }
    return -1;
}

struct utsname {
    char sysname[65];    /* Operating system name (e.g., "Linux") */
    char nodename[65];   /* Name within network */
    char release[65];    /* Operating system release (e.g., "2.6.28") */
    char version[65];    /* Operating system version */
    char machine[65];    /* Hardware identifier */
#ifdef _GNU_SOURCE
    char domainname[65]; /* NIS or YP domain name */
#endif
};

int sys_uname(struct utsname *buf)
{
    strncpy(buf->sysname, "kfs", sizeof(buf->sysname));
    strncpy(buf->nodename, "jareste-kfs", sizeof(buf->nodename));
    strncpy(buf->release, "0.9", sizeof(buf->release));
    strncpy(buf->version, "0.1", sizeof(buf->version));
    strncpy(buf->machine, "i386", sizeof(buf->machine));
    return 0;
}

char *sys_getcwd(char *buf, size_t size)
{
    const char *cwd = "/workspaces/kfs"; // Solo soportamos la raíz
    size_t len = strlen(cwd);
    if (len + 1 > size)
        return NULL; // Buffer demasiado pequeño
    strncpy(buf, cwd, size);
    return buf;
}

// ioctl requests más comunes de busybox
#define TCGETS      0x5401
#define TIOCGWINSZ  0x5413
#define TIOCSWINSZ  0x5414
#define TCSETSW     0x5403
#define TCSETSF     0x5404
#define TCSETS      0x5402

struct winsize {
    uint16_t ws_row;
    uint16_t ws_col;
    uint16_t ws_xpixel;
    uint16_t ws_ypixel;
};

// termios simplificado
struct termios {
    uint32_t c_iflag;
    uint32_t c_oflag;
    uint32_t c_cflag;
    uint32_t c_lflag;
    uint8_t  c_line;
    uint8_t  c_cc[19];
};

int sys_ioctl(int fd, uint32_t request, void *arg)
{
    task_t *task = get_current_task();

    if (fd < 0 || fd >= MAX_FDS || !task->fd_table[fd])
        return -1;

    switch (request)
    {
        case TIOCGWINSZ: {
            if (!arg) return -1;
            struct winsize *ws = (struct winsize*)arg;
            ws->ws_row    = 25;
            ws->ws_col    = 80;
            ws->ws_xpixel = 0;
            ws->ws_ypixel = 0;
            return 0;
        }
        case TIOCSWINSZ:
            return 0;

        case TCGETS:
            if (!arg) return -1;
            struct termios *t = (struct termios*)arg;
            memset(t, 0, sizeof(*t));
            t->c_iflag = 0x6d02;  // BRKINT|ICRNL|IXON|IXANY
            t->c_oflag = 0x0005;  // OPOST|ONLCR
            t->c_cflag = 0x04bf;  // CS8|CREAD|HUPCL|B38400
            t->c_lflag = 0x8a3b & ~0x8u;  // ISIG|ICANON|ECHOE|ECHOK|IEXTEN, no ECHO (0x8)
            t->c_cc[4] = 1;       // VMIN
            t->c_cc[5] = 0;       // VTIME
            return 0;
        case TCSETS:
        case TCSETSW:
        case TCSETSF:
            return 0; // acepta cambios pero los ignora

        default:
            return -1;
    }
}

int sys_fcntl(int fd, int cmd, uint32_t arg)
{
    task_t *task = get_current_task();

#define F_DUPFD         0
#define F_GETFD         1
#define F_SETFD         2
#define F_GETFL         3
#define F_SETFL         4
#define F_DUPFD_CLOEXEC 1030

    if (fd < 0 || fd >= MAX_FDS || !task->fd_table[fd])
        return -9; // -EBADF

    switch (cmd)
    {
        case F_DUPFD:
        case F_DUPFD_CLOEXEC:
        {
            int newfd;
            uint32_t min_fd = arg;
            for (newfd = (int)min_fd; newfd < MAX_FDS; newfd++)
            {
                if (!task->fd_table[newfd])
                    break;
            }
            if (newfd >= MAX_FDS)
                return -24; // -EMFILE

            task->fd_table[newfd] = true;
            task->fd_pointers[newfd] = task->fd_pointers[fd];
            task->fd_pointers[newfd].ref_count++;
            return newfd;
        }
        case F_GETFD:
        case F_GETFL:
            return 0;
        case F_SETFD:
        case F_SETFL:
            return 0;
        default:
            return -38; // -ENOSYS
    }
}

struct timespec64 {
    int64_t  tv_sec;
    int32_t  tv_nsec;
};

#define CLOCK_REALTIME  0
#define CLOCK_MONOTONIC 1

int sys_clock_gettime64(int clockid, struct timespec64 *tp)
{
    if (!tp)
        return -1;

    if (clockid == CLOCK_REALTIME)
        tp->tv_sec = _time(NULL); /* real wall-clock time, from the RTC */
    else
        tp->tv_sec = get_kuptime(); /* monotonic: seconds since boot */
    tp->tv_nsec = 0;
    return 0;
}

struct timespec32 {
    int32_t tv_sec;
    int32_t tv_nsec;
};

int sys_clock_gettime(int clockid, struct timespec32 *tp)
{
    if (!tp) return -1;

    if (clockid == CLOCK_REALTIME)
        tp->tv_sec = _time(NULL);
    else
        tp->tv_sec = get_kuptime();
    tp->tv_nsec = 0;
    return 0;
}

struct sysinfo32 {
    long uptime;
    unsigned long loads[3];
    unsigned long totalram;
    unsigned long freeram;
    unsigned long sharedram;
    unsigned long bufferram;
    unsigned long totalswap;
    unsigned long freeswap;
    unsigned short procs, pad;
    unsigned long totalhigh;
    unsigned long freehigh;
    unsigned int mem_unit;
    char __reserved[256];
};

/* It's not 100% correct, as i'm only setting uptime and mem_unit. for proper handling I should be checking also everything else. */
int sys_sysinfo(struct sysinfo32 *info)
{
    if (!info)
        return -14; // -EFAULT

    memset(info, 0, sizeof(*info));
    info->uptime = (long)get_kuptime();
    info->mem_unit = 1;
    return 0;
}

int sys_llseek(unsigned int fd, uint32_t offset_high, uint32_t offset_low, ssize_t *result, int whence)
{
    ssize_t new_pos;
    (void)offset_high;

    if (!result)
        return -14; // -EFAULT

    new_pos = sys_lseek((int)fd, (ssize_t)offset_low, whence);
    if (new_pos < 0)
        return -1;

    *result = new_pos;
    return 0;
}

int sys_wait4(pid_t pid, int *status, int options, void *rusage);
struct iovec
{
    void  *iov_base;
    size_t iov_len;
};

int sys_writev(int fd, const struct iovec *iov, int iovcnt)
{
    if (!iov || iovcnt <= 0)
        return -1;

    int total = 0;
    for (int i = 0; i < iovcnt; i++)
    {
        if (!iov[i].iov_base || iov[i].iov_len == 0)
            continue;
        int n = _sys_write(fd, iov[i].iov_base, iov[i].iov_len);
        if (n < 0)
            return total > 0 ? total : -1;
        total += n;
    }
    return total;
}

int sys_dup2(int oldfd, int newfd)
{ 
    task_t *t = get_current_task();
    if (oldfd < 0 || oldfd >= MAX_FDS || !t->fd_table[oldfd])
        return -1;
    if (newfd < 0 || newfd >= MAX_FDS)
        return -1;
    if (newfd == oldfd)
        return newfd;
    if (t->fd_table[newfd])
        sys_close(newfd, t);
    memcpy(&t->fd_pointers[newfd], &t->fd_pointers[oldfd], sizeof(file_t));
    t->fd_table[newfd] = true;
    return newfd;
}

int sys_brk(uint32_t new_brk);
int sys_mprotect(uint32_t addr, size_t len, int prot);
int sys_statx(int dirfd, const char *path, int flags,
              unsigned int mask, void *statxbuf);
int sys_fadvise64(int fd, off_t offset, off_t len, int advice);
int syscall_handler(iret_regs_t* reg)
{
    uint32_t syscall_number = reg->eax;
    uint32_t arg1 = reg->ebx;
    uint32_t arg2 = reg->ecx;
    uint32_t arg3 = reg->edx;
    uint32_t arg4 = reg->esi;
    uint32_t arg5 = reg->edi;
    uint32_t arg6 = reg->ebp;

    if (syscall_number == SYS_FORK)
    {
        return sys_fork(reg);
    }

    if (syscall_number >= SYS_MAX_SYSCALL || syscall_table[syscall_number].handler.handler == NULL)
    {
        /* Unimplemented syscall
         */
        kprintf("Unknown syscall: %d\n", syscall_number);
        return -38; // -ENOSYS
    }

    syscall_entry_t entry = syscall_table[syscall_number];

    // kprintf("Syscall: %d\n", syscall_number);
    syscall_return_t ret_value;
    switch (entry.num_args)
    {
        case 0:
            ret_value.int_value = ((syscall_handler_0_t)entry.handler.handler)();
            break;
        case 1:
            ret_value.int_value = ((syscall_handler_1_t)entry.handler.handler)(arg1);
            break;
        case 2:
            ret_value.int_value = ((syscall_handler_2_t)entry.handler.handler)(arg1, arg2);
            break;
        case 3:
            ret_value.int_value = ((syscall_handler_3_t)entry.handler.handler)(arg1, arg2, arg3);
            break;
        case 4:
            ret_value.int_value = ((syscall_handler_4_t)entry.handler.handler)(arg1, arg2, arg3, arg4);
            break;
        case 5:
            ret_value.int_value = ((syscall_handler_5_t)entry.handler.handler)(arg1, arg2, arg3, arg4, arg5);
            break;
        case 6:
            ret_value.int_value = ((syscall_handler_6_t)entry.handler.handler)(arg1, arg2, arg3, arg4, arg5, arg6);
            break;
        default:
            kprintf("Invalid number of arguments for syscall %d\n", syscall_number);
            ret_value.int_value = -1;
            break;
    }

    return ret_value.int_value;
}

void* sys_mmap2(uint32_t addr, size_t length, int prot,
                int flags, int fd, uint32_t pgoffset);
int sys_munmap(void *addr, size_t length);
int sys_rseq(void *rseq_ptr, uint32_t rseq_len, int flags, uint32_t sig);
void init_syscalls()
{
    memset(syscall_table, 0, sizeof(syscall_table));

    syscall_table[SYS_EXIT] = (syscall_entry_t){ .ret_value_entry = RET_INT, .num_args = 1, .handler.handler = (void*)sys_exit };
    syscall_table[SYS_WRITE] = (syscall_entry_t){ .ret_value_entry = RET_SIZE, .num_args = 3, .handler.handler = (void*)_sys_write, };
    syscall_table[SYS_READ] = (syscall_entry_t){ .ret_value_entry = RET_SIZE, .num_args = 3, .handler.handler = (void*)_sys_read };
    syscall_table[SYS_GETDENTS64] = (syscall_entry_t){ .ret_value_entry = RET_SIZE, .num_args = 3, .handler.handler = (void*)_sys_getdents64 };
    syscall_table[SYS_OPEN] = (syscall_entry_t){ .ret_value_entry = RET_INT, .num_args = 2, .handler.handler = (void*)_sys_open };
    syscall_table[SYS_CHMOD] = (syscall_entry_t){ .ret_value_entry = RET_INT, .num_args = 2, .handler.handler = (void*)sys_chmod };
    syscall_table[SYS_CHDIR] = (syscall_entry_t){ .ret_value_entry = RET_INT, .num_args = 1, .handler.handler = (void*)sys_chdir };
    syscall_table[SYS_STATFS64] = (syscall_entry_t){ .ret_value_entry = RET_INT, .num_args = 3, .handler.handler = (void*)sys_statfs64 };
    syscall_table[SYS_SYSINFO] = (syscall_entry_t){ .ret_value_entry = RET_INT, .num_args = 1, .handler.handler = (void*)sys_sysinfo };
    syscall_table[SYS__LLSEEK] = (syscall_entry_t){ .ret_value_entry = RET_INT, .num_args = 5, .handler.handler = (void*)sys_llseek };
    syscall_table[SYS_CLOSE] = (syscall_entry_t){ .ret_value_entry = RET_INT, .num_args = 1, .handler.handler = (void*)_sys_close, };
    syscall_table[SYS_GETPID] = (syscall_entry_t){ .ret_value_entry = RET_INT, .num_args = 0, .handler.handler = (void*)sys_get_pid };
    syscall_table[SYS_SIGNAL] = (syscall_entry_t){ .ret_value_entry = RET_INT, .num_args = 2, .handler.handler = (void*)sys_signal };
    syscall_table[SYS_KILL] = (syscall_entry_t){ .ret_value_entry = RET_INT, .num_args = 2, .handler.handler = (void*)sys_kill };
    syscall_table[SYS_EXECVE] = (syscall_entry_t){ .ret_value_entry = RET_INT, .num_args = 3, .handler.handler = (void*)sys_execve };
    syscall_table[SYS_NANOSLEEP] = (syscall_entry_t){ .ret_value_entry = RET_VOID, .num_args = 1, .handler.handler = (void*)sys_usleep };
    syscall_table[SYS_SLEEP] = (syscall_entry_t){ .ret_value_entry = RET_VOID, .num_args = 1, .handler.handler = (void*)sys_sleep };
    syscall_table[SYS_PRCTL] = (syscall_entry_t){ .ret_value_entry = RET_INT, .num_args = 5, .handler.handler = (void*)sys_prctl };
    syscall_table[SYS_TIME] = (syscall_entry_t){ .ret_value_entry = RET_INT, .num_args = 1, .handler.handler = (void*)sys_time };
    syscall_table[SYS_SET_THREAD_AREA] = (syscall_entry_t){ .ret_value_entry = RET_INT, .num_args = 1, .handler.handler = (void*)sys_set_thread_area };
    syscall_table[SYS_SET_TID_ADDRESS] = (syscall_entry_t){ .ret_value_entry = RET_INT, .num_args = 1, .handler.handler = (void*)sys_set_tid_address };
    /* TODO maybe create sys_exit_group ?*/
    syscall_table[SYS_EXIT_GROUP] = (syscall_entry_t){ .ret_value_entry = RET_VOID, .num_args = 1, .handler.handler = (void*)sys_exit };
    syscall_table[SYS_BRK] = (syscall_entry_t){ .ret_value_entry = RET_INT, .num_args = 1, .handler.handler = (void*)sys_brk };
    syscall_table[SYS_MPROTECT] = (syscall_entry_t){ .ret_value_entry = RET_INT, .num_args = 3, .handler.handler = (void*)sys_mprotect };
    syscall_table[SYS_MMAP2] = (syscall_entry_t){ .ret_value_entry = RET_PTR, .num_args = 6, .handler.handler = (void*)sys_mmap2 };
    syscall_table[SYS_MUNMAP] = (syscall_entry_t){ .ret_value_entry = RET_INT, .num_args = 2, .handler.handler = (void*)sys_munmap, };
    syscall_table[SYS_DUP2] = (syscall_entry_t){ .ret_value_entry = RET_INT, .num_args = 2, .handler.handler = (void*)sys_dup2 };
    syscall_table[SYS_SET_ROBUST_LIST] = (syscall_entry_t){ .num_args=2, .handler.handler=(void*)sys_set_robust_list };
    syscall_table[SYS_RSEQ] = (syscall_entry_t){ .num_args=4, .handler.handler=(void*)sys_rseq };
    syscall_table[SYS_UGETRLIMIT] = (syscall_entry_t){ .num_args=2, .handler.handler=(void*)sys_ugetrlimit };
    syscall_table[SYS_GETPPID]  = (syscall_entry_t){ .num_args=0, .handler.handler=(void*)sys_getppid };
    syscall_table[SYS_GETEUID32] = (syscall_entry_t){ .num_args=0, .handler.handler=(void*)sys_geteuid32 };
    syscall_table[SYS_GETGID32] = (syscall_entry_t){ .num_args=0, .handler.handler=(void*)sys_getgid32 };
    syscall_table[SYS_SETGID32] = (syscall_entry_t){ .num_args=1, .handler.handler=(void*)sys_setgid32 };
    syscall_table[SYS_SETUID32] = (syscall_entry_t){ .num_args=1, .handler.handler=(void*)sys_setuid32 };
    syscall_table[SYS_GETUID32] = (syscall_entry_t){ .num_args=0, .handler.handler=(void*)sys_getuid32 };
    syscall_table[SYS_GETRANDOM] = (syscall_entry_t){ .num_args=3, .handler.handler=(void*)sys_getrandom };
    syscall_table[SYS_RT_SIGACTION] = (syscall_entry_t){ .num_args=4, .handler.handler=(void*)sys_rt_sigaction };
    syscall_table[SYS_READLINKAT] = (syscall_entry_t){ .num_args=4, .handler.handler=(void*)sys_readlinkat };
    syscall_table[SYS_IOCTL]  = (syscall_entry_t){ .num_args=3, .handler.handler=(void*)sys_ioctl };
    syscall_table[SYS_FCNTL]  = (syscall_entry_t){ .ret_value_entry = RET_INT, .num_args=3, .handler.handler=(void*)sys_fcntl };
    syscall_table[SYS_FCNTL64] = (syscall_entry_t){ .ret_value_entry = RET_INT, .num_args=3, .handler.handler=(void*)sys_fcntl };
    syscall_table[SYS_STATX] = (syscall_entry_t){ .num_args=5, .handler.handler=(void*)sys_statx };
    syscall_table[SYS_UNAME] = (syscall_entry_t){ .num_args=1, .handler.handler=(void*)sys_uname };
    syscall_table[SYS_GETCWD] = (syscall_entry_t){ .num_args=2, .handler.handler=(void*)sys_getcwd };
    syscall_table[SYS_WAIT4] = (syscall_entry_t){ .ret_value_entry = RET_INT, .num_args = 4, .handler.handler = (void*)sys_wait4 };
    syscall_table[SYS_WRITEV] = (syscall_entry_t){ .ret_value_entry = RET_INT, .num_args = 3, .handler.handler = (void*)sys_writev };
    syscall_table[SYS_SIGPROCMASK] = (syscall_entry_t){ .ret_value_entry = RET_INT, .num_args = 3, .handler.handler = (void*)sys_rt_sygprocmask };
    syscall_table[403] = (syscall_entry_t){ .ret_value_entry = RET_INT, .num_args = 2, .handler.handler = (void*)sys_clock_gettime64 };

    syscall_table[265] = (syscall_entry_t){
        .num_args = 2,
        .handler.handler = (void*)sys_clock_gettime,
    };

    syscall_table[SYS_FADVISE64] = (syscall_entry_t){ .ret_value_entry = RET_INT, .num_args = 4, .handler.handler = (void*)sys_fadvise64 };

}
