#ifndef TASK_H
#define TASK_H

#include "../utils/stdint.h"
#include "../keyboard/signals.h"
#include "../utils/utils.h"
#include "../syscalls/syscalls.h"
#include "cpu_state.h"
#include "env.h"
#include "../ide/ext2_fileio.h"
#include "../memory/vmm.h"

#define KFS_USER_STUB_ADDR      0xBF000000u
#define KFS_SIGRETURN_STUB_ADDR (KFS_USER_STUB_ADDR + 0x100u)

typedef enum
{
    TASK_RUNNING,
    TASK_READY,
    TASK_ZOMBIE,
    TASK_WAITING,
    TASK_SLEEPING,
    TASK_TO_DIE
} task_state_t;

typedef struct child_list
{
    struct task_struct *task;
    struct child_list *next;
} child_list_t;

struct rseq {
    uint32_t cpu_id_start;
    uint32_t cpu_id;
    uint64_t rseq_cs;
    uint32_t flags;
    uint32_t node_id;
    uint32_t mm_cid;
} __attribute__((packed));

typedef struct task_struct
{
    cpu_state_t cpu;
    uint32_t cpu_esp_;
    uint32_t pid;
    uintptr_t kernel_stack; // Kernel Stack (for syscalls)
    uintptr_t kernel_stack_base; // Base of the kernel stack (for freeing)
    uintptr_t stack;        // User Stack

    page_directory_t* page_dir;

    env_hashtable_t *env; /* should not be used from the kernel itself (? */
    uint32_t gs; /* Added to save/restore GS segment register */
    uint32_t tls_base;

    struct task_struct *parent;
    struct task_struct *next;
    child_list_t *children;
    task_state_t state;
    char name[16];
    void (*on_exit)(void);
    union
    {
        void (*entry)(void);
        void (*entry_env)(char**);
    };
    signal_context_t signals;
    size_t owner;
    void *mem_block;      // pointer to the big allocation
    size_t block_size;    // total size of the allocation
    int exit_status;
    uid_t uid;
    uid_t euid;
    gid_t gid;
    bool is_user;
    uint64_t wake_tick;
    uintptr_t stub_page;

    bool screen_echo;

    file_t fd_pointers[MAX_FDS];
    bool fd_table[MAX_FDS];

    uint32_t brk_start;
    uint32_t brk_current;

    void* rseq_ptr;
    char** argv;
    int argc;

    iret_regs_t saved_sig_frame;
    bool sig_frame_valid;
    /* missing fields but untill it'll not work makes no sense to add them */
} task_t;

void pause_scheduler(int pause);
void start_foo_tasks(void);
void scheduler_init(void);
task_t* get_task_by_pid(pid_t pid);
task_t* get_current_task();
pid_t get_max_pid(void);
pid_t get_foreground_pid(void);
void set_foreground_pid(pid_t pid);
void kill_task();
pid_t _waitpid(pid_t pid, int *status, int options);
pid_t _getpid(void);
int m_get_scheduler_running(void);
void schedule_task_sleep(task_t* task, uint64_t seconds);

pid_t create_user_task_at(uint32_t entry_addr, const char *name, void (*on_exit)(void), page_directory_t* _task_dir, uint32_t heap_start, char* const argv[], char* const envp[]);

void _exit(int status);

pid_t _fork(iret_regs_t* parent_frame);

void start_user();
void schedule(void);
/* Field offsets used by asm are computed via offsetof() in task_offsets.h,
 * which supersedes the extern globals that used to live here. */
// extern task_t* current_task;

#endif
