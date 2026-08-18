#include "task.h"
#include "../memory/memory.h"
#include "../memory/vmm.h"
#include "../memory/pmm.h"
#include "../utils/utils.h"
#include "../utils/stdint.h"
#include "../display/display.h"
#include "../keyboard/signals.h"
#include "../keyboard/idt.h"
#include "../gdt/gdt.h"
#include "../kshell/kshell.h"
#include "../utils/queue.h"
#include "../sockets/socket.h"
#include "../display/tty/tty.h"
#include "elf.h"

#define STACK_SIZE 4096
#define MAX_ACTIVE_TASKS 100

typedef struct __attribute__((packed))
{
    uint32_t edi, esi, ebp, esp_dummy;
    uint32_t ebx, edx, ecx, eax;
    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;
} irq_frame_t;

void kernel_main();
void task_1(void);
void task_1_exit();
static void task_exit_pid(pid_t task_id) __attribute__((unused));
static void task_exit_task(task_t* task, int singal);
extern void switch_context(task_t *prev, task_t *next);
extern void switch_context_to_user(task_t *prev, task_t *next);
extern void copy_context(task_t *prev, task_t *next);
void show_tasks();

static command_t commands[] = {
    {"show", "Show active tasks", show_tasks},
    {NULL, NULL, NULL}
};

static char* default_envp[] __attribute__((unused)) = {
    "PATH=/bin:/usr/bin",
    "HOME=/",
    "USER=root",
    "SHELL=/bin/ushell",
    NULL
};

static uint8_t user_code[] __attribute__((unused)) =
{
    // write(1, msg, 10)
    0xB8, 0x04, 0x00, 0x00, 0x00,   // mov eax, 4
    0xBB, 0x01, 0x00, 0x00, 0x00,   // mov ebx, 1
    0xB9, 0x00, 0x00, 0x00, 0x00,   // mov ecx, <addr>
    0xBA, 0x0A, 0x00, 0x00, 0x00,   // mov edx, 10
    0xCD, 0x30,                      // int 0x30  (write)

    // exit(0)
    0xB8, 0x01, 0x00, 0x00, 0x00,   // mov eax, 1
    0xBB, 0x00, 0x00, 0x00, 0x00,   // mov ebx, 0
    0xCD, 0x30,                      // int 0x30  (exit)
};


static uint8_t user_msg[] __attribute__((unused)) = "User task\n";

/* Stub for exiting user tasks */
static uint8_t exit_stub[] =
{
    0xB8, 0x01, 0x00, 0x00, 0x00,  // mov eax, 1  (SYS_EXIT)
    0x31, 0xDB,                      // xor ebx, ebx
    0xCD, 0x30,                      // int 0x30
    0xEB, 0xFE,                      // jmp $ (por si acaso)
};

/* Return address for a real (userspace) signal handler's call frame (see
 * maybe_deliver_signal()): once the handler does a normal `ret`, execution
 * lands here instead of wherever it would have gone in the interrupted
 * code, and this asks the kernel to restore that interrupted context
 * in-place via sys_sigreturn(). Shares USER_STUB_ADDR's page with
 * exit_stub, at a fixed offset into it. */
static uint8_t sigreturn_stub[] =
{
    0xB8, 0x90, 0x01, 0x00, 0x00,  // mov eax, 400  (SYS_KFS_SIGRETURN)
    0xCD, 0x30,                      // int 0x30
    0xEB, 0xFE,                      // jmp $ (should never get here)
};

static uint32_t *saved_kernel_esp __attribute__((unused)) = NULL;
static uint32_t m_let_scheduler_run = 0;
task_t* current_task = NULL;
static task_t* task_list = NULL;
static task_t* to_free = NULL;
static pid_t task_index = 0;
static Queue finished_pid_queue;

static pid_t m_foreground_pid = -1;

pid_t get_foreground_pid(void)
{
    return m_foreground_pid;
}

void set_foreground_pid(pid_t pid)
{
    m_foreground_pid = pid;
}

void dtach_from_childs(task_t *task)
{
    child_list_t* current;
    child_list_t* next;

    current = task->children;
    while (current)
    {
        current->task->parent = NULL;
        next = current->next;
        kfree(current);
        current = next;
    }
    task->children = NULL;
}

void remove_from_father(task_t* task)
{
    if (!task->parent)
        return;
    child_list_t *current = task->parent->children;
    child_list_t *prev = NULL;
    while (current)
    {
        if (current->task == task)
        {
            if (prev)
                prev->next = current->next;
            else
                task->parent->children = current->next;
            kfree(current);
            break;
        }
        prev = current;
        current = current->next;
    }
}

void free_envp(task_t* task)
{
    env_hashtable_destroy(task->env);
}

void close_all_fds(task_t* task)
{
    uint32_t i;
    file_t* f;

    for (i = 0; i < MAX_FDS; i++)
    {
        if (!task->fd_table[i])
            continue;

        f = &task->fd_pointers[i];
        if (f->fops.close && f->fp)
            f->fops.close(f->fp);

        f->fp = NULL;
        task->fd_table[i] = false;
    }
}

void free_finished_tasks()
{
    uint32_t i;
    uint32_t j;
    uint32_t virt_start;
    bool cloneable;
    bool has_user_pages;
    page_table_t *tbl;
    static bool busy = false;

    if (!to_free || busy)
        return;
    busy = true;

    dtach_from_childs(to_free);
    remove_from_father(to_free);
    close_all_fds(to_free);
    free_envp(to_free);
    kfree((void*)to_free->kernel_stack_base);
    if (to_free->is_user)
    {
        // vfree((void*)to_free->stack);
        /* do nothing */
    }
    else
        kfree((void*)to_free->stack);

    if (to_free->page_dir)
    {
        for (i = 0; i < 1024; i++)
        {
            virt_start = i * 4 * 1024 * 1024;
            cloneable = ((virt_start >= 0x08000000) && (virt_start < 0x0C000000)) ||
                              ((virt_start >= 0xBF000000) && (virt_start < 0xC0000000)) ||
                              ((virt_start >= 0xD0000000) && (virt_start < 0xF0000000));
            if (!cloneable || !to_free->page_dir->entries[i])
                continue;

            tbl = (page_table_t*)
                (to_free->page_dir->entries[i] & 0xFFFFF000);

            has_user_pages = false;
            for (j = 0; j < 1024; j++)
            {
                if ((tbl->entries[j] & PAGE_PRESENT) && (tbl->entries[j] & PAGE_USER))
                {
                    has_user_pages = true;
                    break;
                }
            }
            if (!has_user_pages)
                continue;

            for (j = 0; j < 1024; j++)
            {
                if ((tbl->entries[j] & PAGE_PRESENT) && (tbl->entries[j] & PAGE_USER))
                    pmm_free_frame(tbl->entries[j] & 0xFFFFF000);
            }
            pmm_free_frame((uint32_t)tbl);
        }
        pmm_free_frame((uint32_t)to_free->page_dir);
        to_free->page_dir = NULL;
    }
    kfree(to_free);
    to_free = NULL;
    busy = false;
}

task_t* get_current_task()
{
    return current_task;
}

pid_t get_max_pid(void)
{
    return task_index;
}

pid_t _getpid()
{
    if (!current_task)
        return 0;
    return current_task->pid;
}

uid_t get_current_uid()
{
    return current_task->uid;
}

uid_t get_current_euid()
{
    return current_task->euid;
}

gid_t get_current_gid()
{
    return current_task->gid;
}

void set_current_uid(uid_t uid)
{
    current_task->uid = uid;
}

void set_current_euid(uid_t euid)
{
    current_task->euid = euid;
}

void set_current_gid(gid_t gid)
{
    current_task->gid = gid;
}

void schedule_task_sleep(task_t* task, uint64_t seconds)
{
    task->state = TASK_SLEEPING;
    task->wake_tick = seconds;
    while (task->state == TASK_SLEEPING)
    {
        enable_interrupts();

        if (task->signals.pending_signals & ~task->signals.blocked_signals)
        {
            task->state = TASK_READY;
            break;
        }

        asm volatile("hlt");
    }
}

pid_t _wait(int* status)
{
    data_t data;
    pid_t pid = dequeue(&finished_pid_queue, &data);
    if (pid == 0)
    {
        while (pid == 0)
        {
            pid = dequeue(&finished_pid_queue, &data);
        }
    }
    if (status)
        *status = data.status;
    return data.pid;
}

pid_t _waitpid(pid_t pid, int *status, int options)
{
    (void)options;
    task_t *child = get_task_by_pid(pid);
    if (!child || child->parent != current_task)
        return -1;

    if (current_task->pid == (uint32_t)m_foreground_pid)
    {
        if (child->fd_table[0] && child->fd_pointers[0].type == FD_TTY)
            m_foreground_pid = pid;
        else if (!(current_task->fd_table[0] && current_task->fd_pointers[0].type == FD_TTY))
            m_foreground_pid = current_task->parent ? (pid_t)current_task->parent->pid : m_foreground_pid;
    }

    current_task->state = TASK_WAITING;
    while (child->state != TASK_ZOMBIE)
    {
        enable_interrupts();
        asm volatile("hlt");
    }

    if (status)
        *status = child->exit_status;

    return pid;
}

int sys_wait4(pid_t pid, int *status, int options, void *rusage)
{
    (void)rusage;
    task_t *current = get_current_task();
    child_list_t *c;
    pid_t ret;

    if (pid == -1)
    {
        while (1)
        {
            c = current->children;
            while (c)
            {
                if (c->task->state == TASK_ZOMBIE)
                {
                    ret = c->task->pid;
                    if (status)
                        *status = c->task->exit_status;
                    remove_from_father(c->task);
                    return ret;
                }
                c = c->next;
            }

            if (!current->children)
                return -1;

            if (options & 1) // WNOHANG = 1
                return 0;

            current->state = TASK_WAITING;
            while (current->state == TASK_WAITING)
            {
                enable_interrupts();
                asm volatile("hlt");
            }
        }
    }

    return _waitpid(pid, status, options);
}

task_t* get_task_by_pid(pid_t pid)
{
    task_t *current = task_list;
    if (!current)
        return NULL;
    do
    {
        if (current->pid == (uint32_t)pid)
            return current;
        current = current->next;
    } while (current != task_list);
    return NULL;
}

void check_wake_up(task_t* task)
{
    if ((task->state == TASK_SLEEPING) && (get_tick_count() >= task->wake_tick))
    {
        task->state = TASK_READY;
    }
    if (task->signals.pending_signals & ~task->signals.blocked_signals)
    {
        task->state = TASK_READY;
    }
}

task_t* get_next_task(void)
{
    task_t *start   = current_task->next;
    task_t *current = start;

    do
    {
        check_wake_up(current);
        if (current->pid != 0 &&
            ((current->state == TASK_READY)  || (current->state == TASK_RUNNING)))
            return current;
        current = current->next;
    } while (current != start);

    return task_list;
}

void set_run_scheduler(int value)
{
    m_let_scheduler_run = value;
}

/* TODO: this function is used as a kind of lock to prevent the scheduler from
 * running in critical sections, but maybe it would be better to implement a
 * more robust locking mechanism. */
void pause_scheduler(int pause)
{
    static int prev_value = 0;
    if (pause && prev_value == 0)
    {
        prev_value = m_let_scheduler_run;
        m_let_scheduler_run = 0;
    }
    else if (pause == 0 && prev_value != 0)
    {
        m_let_scheduler_run = prev_value;
        prev_value = 0;
    }
}

int m_get_scheduler_running()
{
    return m_let_scheduler_run;
}

uint32_t timer_schedule(uint32_t *iframe_esp)
{
    static int inside_timer_schedule = 0;
    if (!current_task || !task_list || !m_let_scheduler_run || inside_timer_schedule)
        return 0;

    inside_timer_schedule = 1;
    irq_handler_timer();
    free_finished_tasks();

    handle_signals(iframe_esp);

    task_t *next = get_next_task();
    if (next == current_task)
    {
        inside_timer_schedule = 0;
        return 0;
    }

    task_t *prev = current_task;
    __asm__ volatile("mov %%gs, %0" : "=r"(prev->gs));

    prev->cpu.esp_ = (uint32_t)iframe_esp;

    current_task = next;
    if (next->state == TASK_READY || next->state == TASK_RUNNING)
        next->state = TASK_RUNNING;

    tss_set_stack(next->kernel_stack);
    set_active_env(next->env);

    if (next->page_dir)
        vmm_switch_directory(next->page_dir);
    else
        vmm_set_kernel_dir();

    if (next->tls_base)
    {
        gdt_set_entry(TLS_GDT_ENTRY, next->tls_base, 0xFFFFF, 0xF2, 0xC0);
        register_gdt();
    }

    if (next->rseq_ptr)
    {
        struct rseq *r = (struct rseq*)next->rseq_ptr;
        r->cpu_id_start = 0;
        r->cpu_id = 0;
    }
    
    __asm__ volatile("mov %0, %%gs" :: "r"(next->gs));
    inside_timer_schedule = 0;
    return next->cpu.esp_;
}

void add_new_task(task_t* new_task)
{
    if (!task_list)
    {
        task_list = new_task;
        new_task->next = new_task;
        current_task = task_list;
    }
    else
    {
        task_t *current = task_list;
        while (current->next != task_list)
        {
            current = current->next;
        }
        current->next = new_task;
        new_task->next = task_list;
    }
}

static void task_exit_task(task_t *task, int signal)
{
    if (task->on_exit)
        task->on_exit();

    /* Unlink from the circular list */
    task_t *prev = task_list;
    while (prev->next != task)
        prev = prev->next;

    if (prev == task)
    {
        /* Only one task left — list becomes empty */
        task_list = NULL;
    }
    else
    {
        prev->next = task->next;
        if (task_list == task)
            task_list = task->next;
    }

    task->state = TASK_ZOMBIE;
    task->exit_status = signal;

    if (task->pid == (uint32_t)m_foreground_pid)
        m_foreground_pid = task->parent ? (pid_t)task->parent->pid : m_foreground_pid;

    if (to_free)
    {
        /* If there's already a task waiting to be freed, free it now */
        free_finished_tasks();
    }
    to_free = task;

    /* should we call scheduler()? */
}

static void task_exit_pid(pid_t task_id)
{
    task_t *task = get_task_by_pid(task_id);
    task_exit_task(task, 0);
}

/* Cb function to be called once a task returns
*/
static void task_exit() __attribute__((unused));
static void task_exit()
{
    task_exit_task(current_task, 0);
}

void _exit(int status)
{
    task_t *task = current_task;
    task_t *parent = task->parent;

    task_exit_task(task, status);

    if (parent && parent->state == TASK_WAITING)
        parent->state = TASK_READY;
}

void kill_task(int signal)
{
    // kprintf("Killing task %d With signal:%d--------------\n", current_task->pid, signal);
    task_t *task = current_task;
    task_t *parent = task->parent;

    task_exit_task(task, signal);

    if (parent && parent->state == TASK_WAITING)
        parent->state = TASK_READY;
}

void add_child(task_t* parent, task_t* child)
{
    child_list_t *new_child = kmalloc(sizeof(child_list_t));
    new_child->task = child;
    new_child->next = NULL;

    if (!parent->children)
    {
        parent->children = new_child;
    }
    else
    {
        child_list_t *current = parent->children;
        while (current->next)
        {
            current = current->next;
        }
        current->next = new_child;
    }
}

void fork_init_fds(task_t *child, task_t *parent)
{
    int i;

    memcpy(child->fd_table, parent->fd_table, sizeof(parent->fd_table));

    for (i = 0; i < MAX_FDS; i++)
    {
        if (!child->fd_table[i])
            continue;
        memcpy(&child->fd_pointers[i], &parent->fd_pointers[i], sizeof(file_t));
        child->fd_pointers[i].ref_count++;
        if (child->fd_pointers[i].type == FD_TTY && child->fd_pointers[i].fp)
            ((tty_device_t*)child->fd_pointers[i].fp)->ref_count++;
    }
}

/* this is of course not ok. */
void init_standard_fds(task_t *task)
{
    tty_device_t* tty_device = kmalloc(sizeof(tty_device_t));
    memset(tty_device, 0, sizeof(tty_device_t));

    task->fd_table[0] = true;
    open_tty_device(tty_device, &task->fd_pointers[0]);
    
    task->fd_table[1] = true;
    open_tty_device(tty_device, &task->fd_pointers[1]);

    task->fd_table[2] = true;
    open_tty_device(tty_device, &task->fd_pointers[2]);
}

void create_task(void (*entry)(void), char* name, void (*on_exit)(void))
{
    task_t *task;
    uint32_t *stack;
    uint32_t *kernel_stack;

    if (task_index >= MAX_ACTIVE_TASKS)
    {
        puts_color("Max number of tasks reached\n", RED);
        return;
    }
    
    task = kmalloc(sizeof(task_t));
    stack = kmalloc(STACK_SIZE);
    kernel_stack = kmalloc(STACK_SIZE);
    
    memset(stack, 0, STACK_SIZE);
    task->stack = (uint32_t)stack; /* Point to the stack for being able to release it. */

    stack = (uint32_t*)((uint32_t)stack & 0xFFFFFFF0);
    stack += STACK_SIZE / sizeof(uint32_t);

    *--stack = 0x202;
    *--stack = 0x08;
    *--stack = (uint32_t)entry;

    *--stack = 0;
    *--stack = 0;
    *--stack = 0;
    *--stack = 0;
    *--stack = 0;
    *--stack = 0;
    *--stack = 0;
    *--stack = 0;

    task->pid = task_index++;
    task->cpu.esp_ = (uint32_t)stack; // Point to the simulated interrupt frame
    task->state = TASK_READY;
    task->kernel_stack = (uintptr_t)(stack + STACK_SIZE / sizeof(uint32_t));
    task->kernel_stack_base = (uintptr_t)kernel_stack;
    task->stack = 0;
    memcpy(task->name, name, strlen(name) > 15 ? 15 : strlen(name));
    task->name[strlen(name) > 15 ? 15 : strlen(name)] = '\0';
    task->on_exit = on_exit;
    task->entry = entry;
    task->uid = 0;
    task->euid = 0;
    task->gid = 0;
    task->is_user = false;
    task->env = NULL; /* Kernel tasks don't need envp. */
    task->screen_echo = false;
    memset(task->fd_table, 0, sizeof(task->fd_table));
    init_signals(task);
    add_new_task(task);
}

#define PROT_NONE  0x0
#define PROT_READ  0x1
#define PROT_WRITE 0x2
#define PROT_EXEC  0x4

int sys_mprotect(uint32_t addr, size_t len, int prot)
{
    (void)addr; (void)len; (void)prot;
    return 0;
}

#define MAP_ANONYMOUS 0x20
#define MAP_PRIVATE   0x02

void* sys_mmap2(uint32_t addr, size_t length, int prot, int flags, int fd, uint32_t pgoffset)
{
    task_t *task = get_current_task();
    uint32_t pa;
    uint32_t va;
    uint32_t a;

    (void)prot; (void)fd; (void)pgoffset;

    if (!(flags & MAP_ANONYMOUS))
        return (void*)-1;

    if (length == 0)
        return (void*)-1;

    length = PAGE_ALIGN(length);

    if (addr == 0)
    {
        va = PAGE_ALIGN(task->brk_current);
        task->brk_current = va + length;
    }
    else
    {
        va = addr & 0xFFFFF000;
    }

    for (a = va; a < va + length; a += PAGE_SIZE)
    {
        if (vmm_get_physical(task->page_dir, a) != 0)
            continue;
        pa = pmm_alloc_frame();
        memset((void*)pa, 0, PAGE_SIZE);
        vmm_map_page(task->page_dir, a, pa, PAGE_USER_RW);
    }

    return (void*)va;
}

int sys_munmap(void *addr, size_t length)
{
    (void)addr; (void)length;
    if (addr == NULL || length == 0)
        return -1;
    
    
    return 0;
}

int sys_brk(uint32_t new_brk)
{
    task_t *task = get_current_task();

    if (new_brk == 0)
    {
        return task->brk_current;
    }

    if (new_brk < task->brk_start)
        return task->brk_current;

    uint32_t old_brk = task->brk_current;
    uint32_t new_brk_aligned = PAGE_ALIGN(new_brk);
    uint32_t old_brk_aligned = PAGE_ALIGN(old_brk);

    for (uint32_t va = old_brk_aligned; va < new_brk_aligned; va += PAGE_SIZE)
    {
        if (vmm_get_physical(task->page_dir, va) != 0)
            continue;
        uint32_t pa = pmm_alloc_frame();
        vmm_map_page(task->page_dir, va, pa, PAGE_USER_RW);
    }

    task->brk_current = new_brk;
    return new_brk;
}

int sys_rseq(void *rseq_ptr, uint32_t rseq_len, int flags, uint32_t sig)
{
    (void)flags; (void)sig;
    if (!rseq_ptr)
        return -22; // EINVAL
    if (rseq_len < 32)
        return -22;

    task_t *task = get_current_task();
    
    // Si ya está registrado, error
    if (task->rseq_ptr != NULL)
        return -22;

    struct rseq *r = (struct rseq*)rseq_ptr;
    
    // cpu_id_start y cpu_id = 0 (solo tenemos 1 CPU)
    r->cpu_id_start = 0;
    r->cpu_id = 0;
    r->rseq_cs = 0;
    r->flags = 0;
    r->node_id = 0;
    r->mm_cid = 0;

    // Guarda el puntero en la tarea para actualizarlo en context switch
    task->rseq_ptr = rseq_ptr;

    return 0;
}

pid_t _do_fork(iret_regs_t* parent_frame)
{
    task_t* parent = current_task;
    uint32_t current_gs;

    __asm__ volatile("mov %%gs, %0" : "=r"(current_gs));
    parent->gs = current_gs;

    task_t* child = kmalloc(sizeof(task_t));
    if (!child)
        return -1;
    memcpy(child, parent, sizeof(task_t));

    child->pid = task_index++;
    child->state = TASK_READY;
    child->parent = parent;
    child->children = NULL;
    child->sig_frame_valid = false;

    if (parent->is_user && parent->page_dir)
        child->page_dir = vmm_clone_directory(parent->page_dir);

    uint32_t *kstack_base = kmalloc(STACK_SIZE);
    uint32_t *kstack_top = kstack_base + STACK_SIZE / sizeof(uint32_t);
    child->kernel_stack_base = (uintptr_t)kstack_base;
    child->kernel_stack = (uintptr_t)kstack_top;

    uint32_t *ksp = kstack_top;

    /* build iret for child process. */
    *--ksp = parent_frame->ss_user;
    *--ksp = parent_frame->esp_user;
    *--ksp = parent_frame->eflags | 0x200;
    *--ksp = parent_frame->cs;
    *--ksp = parent_frame->eip;

    *--ksp = 0; /* eax = 0 for child */
    *--ksp = parent_frame->ecx;
    *--ksp = parent_frame->edx;
    *--ksp = parent_frame->ebx;
    *--ksp = 0;
    *--ksp = parent_frame->ebp;
    *--ksp = parent_frame->esi;
    *--ksp = parent_frame->edi;


    child->cpu.esp_ = (uint32_t)ksp;
        
    child->stub_page = 0;
    child->stack = 0;
    child->tls_base = parent->tls_base;

    fork_init_fds(child, parent);
    add_child(parent, child);
    init_signals(child);
    add_new_task(child);

    if (parent->pid == (uint32_t)m_foreground_pid)
        m_foreground_pid = child->pid;

    // kprintf("Forked new task '%s' with PID %d from parent PID %d\n", child->name, child->pid, parent->pid);

    return child->pid;
}

pid_t _fork(iret_regs_t* parent_frame)
{
    return _do_fork(parent_frame);
}

void scheduler_init(void)
{
    task_t *idle = kmalloc(sizeof(task_t));
    uint32_t *stack = kmalloc(STACK_SIZE);
    stack += STACK_SIZE / sizeof(uint32_t);

    init_queue(&finished_pid_queue);

    *--stack = 0x202;
    *--stack = 0x08;
    *--stack = (uint32_t)kernel_main;
    task_index = 0;

    idle->pid = task_index++;
    idle->cpu.esp_ = (uint32_t)stack;
    idle->cpu.eip = (uint32_t)kernel_main;
    idle->state = TASK_READY;
    idle->next = idle;
    memcpy(idle->name, "idle", 4);
    idle->name[4] = '\0';
    idle->uid = 0;
    idle->euid = 0;
    idle->gid = 0;
    current_task = idle;
    task_list = idle;
    to_free = NULL;
    init_signals(idle);
    install_all_cmds(commands, TASKS);
}

/*************************************** */
void task_1_exit()
{
    puts_color("Task 1 exited\n", RED);
}

void task_1_sighandler(int signal)
{
    (void)signal;
    puts_color("Task 1: Signal received\n", GREEN);
}

void task_1(void)
{
    // puts("Task 1 Started\n");

    size_t mmap_size = 16 * 1024;
    // void* user_buffer = mmap(NULL, mmap_size, PROT_READ | PROT_WRITE | PROT_USER,
    //                          MAP_ANONYMOUS, -1, 0);
    void* user_buffer = kmalloc(mmap_size);
    if (user_buffer == (void*)-1)
    {
        puts_color("test_mmap: mmap failed!\n", RED);
        return;
    }

    memset(user_buffer, 'A', mmap_size);
    for (size_t i = 0; i < mmap_size; i++)
    {
        if (((char*)user_buffer)[i] != 'A')
        {
            puts_color("test_mmap: memory corruption detected!\n", RED);
            return;
        }
    }

    kfree(user_buffer);
    // puts_color("test_mmap: mmap/munmap test passed\n", GREEN);

    sys_signal(2, task_1_sighandler);

    while (1)
    {
        int return_value;
        return_value = sys_write(3, "Task 1\n", 7);
        if (return_value > 0) // it must fail so not failing it's indeed a fail
        {
            puts_color("Error writing\n", RED);
        }
    }
}

void socket_1()
{

    int sock;
    char* buffer = "Socket 1\n";

    sock = sys_socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        puts_color("Error creating socket\n", RED);
        return;
    }
    sys_bind(sock, "/foo/bar");

    while(1)
    {
        sys_write(sock, buffer, strlen(buffer));
    }

}

void socket_2()
{
    int sock;
    char buffer[10];
    int return_value;
    char* err_str = "Error reading\n";

    sys_sleep(10);
    sock = sys_connect("/foo/bar");
    if (sock < 0)
    {
        puts_color("Error creating socket\n", RED);
        return;
    }

    kprintf("sleeping for 5 seconds '%d'\n", get_kuptime());
    sys_sleep(5);

    while(1)
    {
        return_value = sys_read(sock, buffer, sizeof(buffer));
        if (return_value < 0)
        {
            // enable_print();
            puts_color(err_str, RED);
        }
        // else if (return_value > 0)
        // {
        //     buffer[return_value] = '\0';
        //     puts_color(read_str, GREEN);
        //     puts_color(buffer, GREEN);
        // }
        sys_sleep(3);
    }

}

/* Deliberately unbounded recursion, used to exercise stack-overflow /
 * guard-page handling; GCC would otherwise flag it as a bug. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winfinite-recursion"
void recursion()
{
    static unsigned int i = 0;
    // puts("Recursion\n" );
    kprintf("Recursion %d\n", i++);
    recursion();
}
#pragma GCC diagnostic pop

void test_recursion(void)
{
    recursion();
}

void task_read()
{
    kprintf("Task 4 Started\n");
    int fd;
    char buffer[11];
    int return_value;
    char* buffer_filler = "Task read\n";
    memcpy(buffer, buffer_filler, 10);
    buffer[10] = '\0';
    char* err_str = "Error reading\n";
    char* filename = "/boot/task_read";
    char* fd_str = "fd: %d\n";

    fd_str = "fd: '%d'\n";
    fd = sys_open(filename, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0)
    {
        puts_color("Failed to open /boot/task_read\n", RED);
        return;
    }

    kprintf(fd_str, fd);

    sys_write(fd, buffer, 10);
    sys_close(fd, get_current_task());

    memset(buffer, 0, sizeof(buffer));
    // while (1)
    // {
        // fd = open("/boot/task_read", O_RDONLY);
        // kprintf("fd------: %d\n", fd);
        // if (fd < 0)
        // {
        //     puts_color("Failed to open /boot/task_read\n", RED);
        //     return;
        // }


    while (1)
    {
        fd = sys_open(filename, O_RDONLY);
        if (fd < 0)
        {
            puts_color("Failed to open /boot/task_read\n", RED);
            return;
        }
        return_value = sys_read(fd, buffer, sizeof(buffer));
        // return_value = read(0, buffer, sizeof(buffer));
        // return_value = write(3, buffer, return_value); // error writing to fd 3
        if (return_value <= 0)
        {
            puts_color("Error reading\n", RED);
        }
        else
        {
            // buffer[return_value] = '\0';
            if (strcmp(buffer, buffer_filler) != 0)
            {
                puts_color(err_str, RED);
            }
            // puts_color(read_str, GREEN);
            // puts_color(buffer, GREEN);
        }
        sys_close(fd, get_current_task());
    }

}

/* think where to place it */
#define USER_STACK_TOP   0xC0000000u
#define USER_STACK_SIZE  (1 * 1024 * 1024)  // 1MB
#define USER_STACK_BASE  (USER_STACK_TOP - USER_STACK_SIZE)
#define USER_STUB_ADDR   KFS_USER_STUB_ADDR
#define SIGRETURN_STUB_ADDR KFS_SIGRETURN_STUB_ADDR

void build_user_stack(uint32_t *usp_out, const char *name, 
                      char * const argv[], char * const envp[],
                      page_directory_t *task_dir)
{
    // Cambia al directorio del task para escribir
    // (asumimos que ya estamos en task_dir)
    (void)task_dir;

    char *stack_top = (char*)USER_STACK_TOP;
    char *str_ptr = stack_top;  // escribe strings desde arriba hacia abajo

    // Cuenta argv y envp
    int argc = 0;
    if (argv) while (argv[argc]) argc++;
    
    int envc = 0;
    if (envp) while (envp[envc]) envc++;

    // Copia las strings al stack (de arriba hacia abajo)
    // Primero envp strings
    char *envp_strs[64];
    for (int i = envc - 1; i >= 0; i--) {
        size_t len = strlen(envp[i]) + 1;
        str_ptr -= len;
        memcpy(str_ptr, envp[i], len);
        envp_strs[i] = str_ptr;
    }

    // Luego argv strings
    char *argv_strs[64];
    if (argc == 0)
    {
        /* No argv so just send the same bin name */
        size_t len = strlen(name) + 1;
        str_ptr -= len;
        memcpy(str_ptr, name, len);
        argv_strs[0] = str_ptr;
    }
    else
    {
        for (int i = argc - 1; i >= 0; i--)
        {
            size_t len = strlen(argv[i]) + 1;
            str_ptr -= len;
            memcpy(str_ptr, argv[i], len);
            argv_strs[i] = str_ptr;
        }
    }

    // Alinea a 16 bytes
    str_ptr = (char*)((uint32_t)str_ptr & ~0xF);

    // Ahora construye el array de punteros (hacia abajo desde str_ptr)
    uint32_t *usp = (uint32_t*)str_ptr;

    // auxv terminator
    *--usp = 0;  // AT_NULL value
    *--usp = 0;  // AT_NULL type

    // envp array (NULL terminated)
    *--usp = 0;  // NULL terminator
    for (int i = envc - 1; i >= 0; i--)
        *--usp = (uint32_t)envp_strs[i];

    // argv array (NULL terminated)
    *--usp = 0;  // NULL terminator
    for (int i = argc - 1; i >= 0; i--)
        *--usp = (uint32_t)argv_strs[i];
    // argv[0] = nombre del binario
    if (argc == 0)
        *--usp = (uint32_t)argv_strs[0];

    // argc
    // Si argv fue NULL, argc real es 1 (solo argv[0] = nombre)
    *--usp = (argc == 0) ? 1 : argc;

    *usp_out = (uint32_t)usp;
}

pid_t create_user_task_at(uint32_t entry_addr, const char *name, void (*on_exit)(void), page_directory_t* _task_dir, uint32_t heap_start, char* const argv[], char* const envp[])
{
    task_t *task = kmalloc(sizeof(task_t));
    memset(task, 0, sizeof(task_t));

    uint32_t *kernel_stack_base = kmalloc(STACK_SIZE*3);
    uint32_t *kernel_stack_top  = kernel_stack_base + STACK_SIZE / sizeof(uint32_t);

    page_directory_t *task_dir = _task_dir ? _task_dir : vmm_current_directory();
    task->page_dir = task_dir;

    for (uint32_t va = USER_STACK_BASE; va < USER_STACK_TOP; va += PAGE_SIZE)
    {
        uint32_t pa = pmm_alloc_frame();
        vmm_map_page(task_dir, va, pa, PAGE_USER_RW);
    }

    uint32_t stub_pa = pmm_alloc_frame();
    vmm_map_page(task_dir, USER_STUB_ADDR, stub_pa, PAGE_USER_RW);

    page_directory_t *prev_dir = vmm_current_directory();
    vmm_switch_directory(task_dir);

    memcpy((void*)USER_STUB_ADDR, exit_stub, sizeof(exit_stub));
    memcpy((void*)SIGRETURN_STUB_ADDR, sigreturn_stub, sizeof(sigreturn_stub));

    uint32_t usp;

    build_user_stack(&usp, name, argv, envp, task_dir);


    vmm_switch_directory(prev_dir);

    uint32_t *ksp = kernel_stack_top;
    *--ksp = 0x2B; /* SS (user) */
    *--ksp = (uint32_t)usp; /* points to argc */
    *--ksp = 0x202; /* EFLAGS with IF=1 to enable interrupts when the task starts running */
    *--ksp = 0x23; /* CS for ring 3 */
    *--ksp = entry_addr;        // EIP

    for (int i = 0; i < 8; i++)
        *--ksp = 0;

    task->cpu.esp_ = (uint32_t)ksp;
    task->kernel_stack = (uintptr_t)kernel_stack_top;
    task->kernel_stack_base = (uintptr_t)kernel_stack_base;
    task->stack = USER_STACK_BASE;
    task->stub_page = USER_STUB_ADDR;
    task->pid = task_index++;
    task->state = TASK_READY;
    task->is_user = true;
    task->on_exit = on_exit;
    task->parent = get_current_task();
    if (task->parent)
        add_child(task->parent, task);

    task->brk_start   = heap_start;
    task->brk_current = heap_start;

    task->uid = 1000; task->euid = 1000; task->gid = 1000;
    task->screen_echo = true;

    memcpy(task->name, name, strlen(name) > 15 ? 15 : strlen(name));
    task->name[15] = '\0';

    memset(task->fd_table, 0, sizeof(task->fd_table));
    if (task->parent && task->parent->fd_table[0])
        fork_init_fds(task, task->parent);
    else
        init_standard_fds(task);
    init_signals(task);
    add_new_task(task);

    return task->pid;
}


void task_wait()
{
    // kprintf("Task 5 Started\n");
    pid_t pid;
    int status;
    while (1)
    {
        pid = _wait(&status);
        (void)pid;
        set_putchar_color(GREEN);
        // kprintf("Task 5: Child %d exited with status %d\n", pid, status);
        set_putchar_color(LIGHT_GREY);
 
        // puts("Task 5\n");
    }
}

void unsleep_kshell()
{
    /* Assuming kshell it's allways 1. */
    task_t *kshell = get_task_by_pid(1);
    kshell->state = TASK_READY;
}

void kshell();
void start_foo_tasks(void)
{
    // create_task(kshell, "kshell", NULL);

    // create_task(task_wait, "task_wait", NULL);
    // create_task(task_1, "task_1", task_1_exit);
    // create_task(task_read, "task_read", NULL);
    // create_task(socket_1, "socket_1", NULL);
    // create_task(socket_2, "socket_2", NULL);
    // exec_bin("/hello");
    // exec_bin("/hello");
    // exec_bin("/hello2");
    // exec_bin("/hello2");
    // exec_bin("/hello2");
    // exec_bin("/hello2");
        static char* busybox_argv[] = {
        "busybox",
        "sh",
        NULL
    };
    static char *default_envp[] = {
        "PATH=/bin:/usr/bin",
        "HOME=/",
        "PWD=/",
        // "TERM=linux",
        NULL
    };

    // static char* busybox_echo_argv[] = {
    //     "busybox",
    //     "echo",
    //     "hola",
    //     NULL
    // };
    set_foreground_pid(exec_bin("busybox", busybox_argv, default_envp));
    // exec_bin("busybox", busybox_echo_argv, default_envp);
    // insmod("/kb_mod.o");

    to_free = NULL;
}

/* ################################################################### */
/*                               TESTS                                 */
/* ################################################################### */
void show_tasks()
{
    task_t *current = task_list;
    do
    {
        kprintf("Task '%s'\n", current->name);
        kprintf("  PID: %d\n", current->pid);
        kprintf("  ESP: %p\n", current->cpu.esp_);
        kprintf("  EIP: %p\n", current->cpu.eip);
        kprintf("  State: %d\n", current->state);
        current = current->next;
    } while (current != task_list);
}
