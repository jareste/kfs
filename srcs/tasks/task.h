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

    /* missing fields but untill it'll not work makes no sense to add them */    
} task_t;

void pause_scheduler(int pause);
void start_foo_tasks(void);
void scheduler_init(void);
task_t* get_task_by_pid(pid_t pid);
task_t* get_current_task();
void kill_task();
pid_t _waitpid(pid_t pid, int *status, int options);

void _exit(int status);

pid_t _fork(iret_regs_t* parent_frame);

void start_user();
#warning "If this is meant to be used, ensure the definition of externs"
void schedule(void);
extern size_t TASK_KERNEL_STACK_OFFSET;
extern size_t TASK_ENV_OFFSET;
extern size_t TASK_PAGE_DIR_OFFSET;
extern size_t TASK_ESP_OFFSET;
// extern task_t* current_task;

#endif
