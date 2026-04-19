#include "task.h"
#include "../memory/memory.h"
#include "../memory/vmm.h"
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

#define STACK_SIZE 4096
#define MAX_ACTIVE_TASKS 100
#define USER_STACK_SIZE 4096

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
static void task_exit_pid(pid_t task_id);
static void task_exit_task(task_t* task, int singal);
extern void switch_context(task_t *prev, task_t *next);
extern void switch_context_to_user(task_t *prev, task_t *next);
extern void copy_context(task_t *prev, task_t *next);
void show_tasks();

static command_t commands[] = {
    {"show", "Show active tasks", show_tasks},
    {NULL, NULL, NULL}
};

static char* default_envp[] = {
    "PATH=/bin:/usr/bin",
    "HOME=/",
    "USER=root",
    "SHELL=/bin/ushell",
    NULL
};

static uint8_t user_code[] =
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


static uint8_t user_msg[] = "User task\n";

/* Stub for exiting user tasks */
static uint8_t exit_stub[] =
{
    0xB8, 0x01, 0x00, 0x00, 0x00,  // mov eax, 1  (SYS_EXIT)
    0x31, 0xDB,                      // xor ebx, ebx
    0xCD, 0x30,                      // int 0x30
    0xEB, 0xFE,                      // jmp $ (por si acaso)
};

static uint32_t *saved_kernel_esp = NULL;
static uint32_t m_let_scheduler_run = 0;
task_t* current_task = NULL;
static task_t* task_list = NULL;
static task_t* to_free = NULL;
static pid_t task_index = 0;
static Queue finished_pid_queue;

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
    for (int i = 0; i < MAX_FDS; i++)
    {
        if (!task->fd_table[i])
            continue;

        file_t *f = &task->fd_pointers[i];
        if (f->fops.close && f->fp)
            f->fops.close(f->fp);

        f->fp = NULL;
        task->fd_table[i] = false;
    }
}

void free_finished_tasks()
{
    if (!to_free)
        return;
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
    // if (to_free->stub_page)
    //     vfree((void*)to_free->stub_page);
#warning "This might not be working, check it out"
    if (to_free->page_dir)
    {
        for (int32_t i = 0; i < 1024; i++)
        {
            uint32_t virt_start = i * 4 * 1024 * 1024;
            if (virt_start < 0x08000000 || virt_start >= 0x0C000000)
                continue;
            if (!to_free->page_dir->entries[i])
                continue;

            page_table_t *tbl = (page_table_t*)
                (to_free->page_dir->entries[i] & 0xFFFFF000);
            for (int32_t j = 0; j < 1024; j++)
            {
                if (tbl->entries[j] & PAGE_PRESENT)
                    pmm_free_frame(tbl->entries[j] & 0xFFFFF000);
            }
            pmm_free_frame((uint32_t)tbl);
        }
        pmm_free_frame((uint32_t)to_free->page_dir);
        to_free->page_dir = NULL;
    }
    kfree(to_free);
    to_free = NULL;
}

task_t* get_current_task()
{
    return current_task;
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
        ;
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
    task_t *child = get_task_by_pid(pid);
    if (!child || child->parent != current_task)
        return -1;
    
    current_task->state = TASK_WAITING;
    while (child->state != TASK_ZOMBIE)
        ;
    
    if (status)
        *status = child->exit_status;
    
    return pid;
}

task_t* get_task_by_pid(pid_t pid)
{
    task_t *current = task_list;
    do
    {
        if (current->pid == pid)
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

#warning "I use this function as a kind of lock to prevent scheduler from running in critical sections, but maybe it would be better to implement a more robust locking mechanism."
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

uint32_t timer_schedule(uint32_t *iframe_esp)
{
    static int inside_timer_schedule = 0;
    if (!current_task || !task_list || !m_let_scheduler_run || inside_timer_schedule)
        return 0;

    inside_timer_schedule = 1;
    irq_handler_timer();
    free_finished_tasks();

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

    pid_t pid = task->pid;
    task->state = TASK_ZOMBIE;
    enqueue(&finished_pid_queue, pid, signal);
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
static void task_exit()
{
    task_exit_task(current_task, 0);
}

void _exit(int status)
{
    task_exit_task(current_task, status);
}

void kill_task(int signal)
{
    // kprintf("Killing task %d With signal:%d--------------\n", current_task->pid, signal);
    task_exit_task(current_task, signal);
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
    memcpy(child->fd_table, parent->fd_table, sizeof(parent->fd_table));
    
    for (int i = 0; i < MAX_FDS; i++)
    {
        if (!child->fd_table[i])
            continue;
        memcpy(&child->fd_pointers[i], &parent->fd_pointers[i], sizeof(file_t));
        child->fd_pointers[i].ref_count++;
#warning could be an issue as each file_t might have pointers to other structures that also need ref counting, but for now it works as is because we only have tty devices that don't have such pointers, just keep it in mind if you add new types of file_t in the future.
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
    memcpy(&task->fd_pointers[2], &task->fd_pointers[1], sizeof(file_t));
    task->fd_pointers[2].ref_count++;
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

    kprintf("Forked new task '%s' with PID %d from parent PID %d\n", child->name, child->pid, parent->pid);

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
    char* read_str = "Read: ";

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

void recursion()
{
    static unsigned int i = 0;
    // puts("Recursion\n" );
    kprintf("Recursion %d\n", i++);
    recursion();
}

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
    char* read_str = "Read :";
    char* err_str = "Error reading\n";
    char* cmp_buff = "Task read\n";
    char* filename = "/boot/task_read";
    char* pointer = "%p '%s'\n";
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
#define USER_STUB_ADDR   0xBF000000u

void create_user_task_at(uint32_t entry_addr, char *name, void (*on_exit)(void), page_directory_t* _task_dir)
{
    task_t *task = kmalloc(sizeof(task_t));
    memset(task, 0, sizeof(task_t));

    uint32_t *kernel_stack_base = kmalloc(STACK_SIZE);
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

    uint32_t *usp = (uint32_t*)USER_STACK_TOP;
    usp = (uint32_t*)((uint32_t)usp & ~0xF);

    *--usp = 0; /* auxv AT_NULL value */
    *--usp = 0; /* auxv AT_NULL type */
    *--usp = 0; /* envp NULL terminator */
    *--usp = 0; /* argv NULL terminator */
    *--usp = 0; /* argc = 0 */

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
    task->uid = 1000; task->euid = 1000; task->gid = 1000;
    task->screen_echo = true;

    memcpy(task->name, name, strlen(name) > 15 ? 15 : strlen(name));
    task->name[15] = '\0';

    memset(task->fd_table, 0, sizeof(task->fd_table));
    init_standard_fds(task);
    init_signals(task);
    add_new_task(task);
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
void ide_task_main(void);
void start_foo_tasks(void)
{
    create_task(kshell, "kshell", NULL);
    create_task(ide_task_main, "ide", NULL);

    create_task(task_wait, "task_wait", NULL);
    create_task(task_1, "task_1", task_1_exit);
    create_task(task_read, "task_read", NULL);
    create_task(socket_1, "socket_1", NULL);
    create_task(socket_2, "socket_2", NULL);
    exec_bin("/hello");
    exec_bin("/hello");
    exec_bin("/hello2");
    exec_bin("/hello2");
    exec_bin("/hello2");
    exec_bin("/hello2");
    insmod("/kb_mod.o");

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
