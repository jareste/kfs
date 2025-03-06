#ifndef TASK_H
#define TASK_H

#include "../utils/stdint.h"
#include "../keyboard/signals.h"
#include "../utils/utils.h"
#include "cpu_state.h"
#include "env.h"
#include "../ide/ext2_fileio.h"

typedef enum
{
    TASK_RUNNING,
    TASK_READY,
    TASK_ZOMBIE,
    TASK_WAITING,
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
    uint32_t cpu_esp_;    // you might keep cpu state in a struct
    uint32_t pid;
    uintptr_t kernel_stack; // Kernel Stack (for syscalls)
    uintptr_t stack;        // User Stack
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

    file_t fd_pointers[MAX_FDS];
    bool fd_table[MAX_FDS];

    env_hashtable_t *env; /* should not be used from the kernel itself (? */
    /* missing fields but untill it'll not work makes no sense to add them */    
} task_t;

void scheduler(void);
void start_foo_tasks(void);
void scheduler_init(void);
task_t* get_task_by_pid(pid_t pid);
task_t* get_current_task();
void kill_task();

void _exit(int status);

pid_t _fork(void);

void start_user();

// extern task_t* current_task;

#endif
