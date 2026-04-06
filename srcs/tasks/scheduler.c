#include "task.h"
#include "../memory/memory.h"
#include "../memory/vmm.h"
#include "../utils/utils.h"
#include "../utils/stdint.h"
#include "../display/display.h"
#include "../keyboard/signals.h"
#include "../gdt/gdt.h"
#include "../kshell/kshell.h"
#include "../utils/queue.h"
#include "../sockets/socket.h"
#include "../display/tty/tty.h"

#define STACK_SIZE 4096
#define MAX_ACTIVE_TASKS 15
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

/* ASM ones */
extern void fork_trampoline(void);
extern void capture_cpu_state(cpu_state_t *state);

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
static task_t* current_task = NULL;
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
        if (task->fd_table[i] == true)
        {
            sys_close(i);
        }
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
        vfree((void*)to_free->stack);
    else
        kfree((void*)to_free->stack);
    if (to_free->stub_page)
        vfree((void*)to_free->stub_page);
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

// task_t* get_next_task()
// {
//     task_t *current = current_task->next;
//     check_wake_up(current);
//     while (current->state == TASK_WAITING || current->state == TASK_SLEEPING)
//     {
//         current = current->next;
//         check_wake_up(current);
//         if (current->pid == 0)
//         {
//             current = current->next;
//         }
//     }
//     return current;
// }

static task_t* get_next_task(void)
{
    task_t *start   = current_task->next;
    task_t *current = start;

    do
    {
        check_wake_up(current);
        if (current->pid != 0 &&
            current->state != TASK_WAITING &&
            current->state != TASK_SLEEPING &&
            current->state != TASK_ZOMBIE &&
            current->state != TASK_TO_DIE)
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
    else
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

    prev->cpu.esp_ = (uint32_t)iframe_esp;

    current_task = next;
    if (next->state == TASK_READY || next->state == TASK_RUNNING)
        next->state = TASK_RUNNING;

    tss_set_stack(next->kernel_stack);
    set_active_env(next->env);

#warning "This might cause problems as by default tasks would not have page dir, so they would be switched to kernel dir, but maybe it's not a problem as kernel tasks don't need it. Just keep it in mind."
    if (next->page_dir)
        vmm_switch_directory(next->page_dir);
    else
        vmm_set_kernel_dir();

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
    // printf("Killing task %d With signal:%d--------------\n", current_task->pid, signal);
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

/* this is of course not ok. */
void init_standard_fds(task_t *task)
{
    tty_device_t* tty_device = kmalloc(sizeof(tty_device_t));
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

pid_t _do_fork(const cpu_state_t *parent_state)
{
    if (task_index >= MAX_ACTIVE_TASKS)
    {
        puts_color("Max number of tasks reached\n", RED);
        return -1;
    }

    task_t *parent = current_task;
    task_t *child = kmalloc(sizeof(task_t));
    if (!child)
        return -1;

    uint32_t live_esp = parent_state->esp_;


    /*
     * Determine the parent's aligned stack top.
     * (This must match the way create_task() computes it.)
     */
    uint32_t *parent_raw_stack = (uint32_t *)parent->stack;
    uint32_t *parent_stack_top = (uint32_t *)((uint32_t)parent_raw_stack & 0xFFFFFFF0);
    parent_stack_top += STACK_SIZE / sizeof(uint32_t);

    /* Calculate the used portion of the parent's stack in bytes.
       (Stack grows downward so: used_bytes = parent_stack_top - live_esp) */
    size_t used_bytes = (uint32_t)parent_stack_top - live_esp;

    /*
     * Allocate and align a new user stack for the child.
     */
    uint32_t *child_raw_stack = kmalloc(STACK_SIZE);
    if (!child_raw_stack)
        return -1;
    child->stack = (uint32_t)child_raw_stack;
    uint32_t *child_stack_top = (uint32_t *)((uint32_t)child_raw_stack & 0xFFFFFFF0);
    child_stack_top += STACK_SIZE / sizeof(uint32_t);

    /*
     * Compute the child's new ESP so that the same amount of stack is used.
     * That is, if parent's ESP is X bytes below parent's top,
     * then child's ESP = child_stack_top - used_bytes.
     */
    uint32_t *child_cpu_esp = (uint32_t *)((uint32_t)child_stack_top - used_bytes);
    memcpy(child_cpu_esp, (void*)live_esp, used_bytes);

    /* 
     * Copy parent's CPU state (captured earlier) into the child.
     */
    memcpy(&child->cpu, parent_state, sizeof(cpu_state_t));
    child->cpu.esp_ = (uint32_t)child_cpu_esp;

    /* --- Adjust the frame pointer (EBP) chain in the copied stack --- */
    {
        uintptr_t parent_top_addr = (uintptr_t)parent_stack_top;
        uintptr_t child_top_addr  = (uintptr_t)child_stack_top;
        uintptr_t delta = child_top_addr - parent_top_addr;

        if (child->cpu.ebp >= live_esp && child->cpu.ebp < parent_top_addr)
        {
            child->cpu.ebp += delta;
            uint32_t *cur_ebp = (uint32_t *)child->cpu.ebp;
            while(cur_ebp &&
                  ((uintptr_t)cur_ebp >= (uintptr_t)child_cpu_esp) &&
                  ((uintptr_t)cur_ebp < child_top_addr))
            {
                uint32_t saved_ebp = *cur_ebp;
                if (saved_ebp >= live_esp && saved_ebp < parent_top_addr)
                {
                    uint32_t new_val = saved_ebp + delta;
                    *cur_ebp = new_val;
                    cur_ebp = (uint32_t *)new_val;
                }
                else
                {
                    break;
                }
            }
        }
    }
    /* --- End EBP fix-up --- */

    /*
     * Insert a trampoline address on the child's stack.
     *
     * When the child is scheduled, the context switch will load its ESP
     * from child->cpu.esp_. A subsequent "ret" will pop the trampoline address,
     * jump to fork_trampoline (which sets EAX to 0), and then "ret" to the
     * original return address.
     */
    uint32_t *child_sp = (uint32_t *)child->cpu.esp_;
    child_sp--;  // reserve space for the trampoline address
    *child_sp = (uint32_t)fork_trampoline;
    child->cpu.esp_ = (uint32_t)child_sp;

    /*
     * Allocate a new kernel stack for the child.
     * (Here we simply allocate a fresh kernel stack rather than copying the parent's.)
     */
    uint32_t *child_kstack_alloc = kmalloc(STACK_SIZE);
    if (!child_kstack_alloc)
        return -1;
    uint32_t *child_kstack_top = (uint32_t *)((uint32_t)child_kstack_alloc & 0xFFFFFFF0);
    child_kstack_top += STACK_SIZE / sizeof(uint32_t);
    child->kernel_stack = (uint32_t)child_kstack_top;

    /* Set up the remainder of the child's task structure. */
    child->pid = task_index++;
    child->state = TASK_READY;
    memcpy(child->name, parent->name, 15);
    child->name[15] = '\0';
    child->on_exit = parent->on_exit;
    child->entry = parent->entry;
    child->parent = parent;

    child->uid        = parent->uid;
    child->euid       = parent->euid;
    child->gid        = parent->gid;
    child->is_user    = parent->is_user;
    child->screen_echo = parent->screen_echo;
    /* Inherit env (shallow copy — each process should ideally have its own) */
    child->env        = parent->env;
    /* Inherit file descriptors */
    memcpy(child->fd_table,    parent->fd_table,    sizeof(parent->fd_table));
    memcpy(child->fd_pointers, parent->fd_pointers, sizeof(parent->fd_pointers));
    /* Increment ref counts on open files */
    for (int i = 0; i < MAX_FDS; i++)
        if (child->fd_table[i])
            child->fd_pointers[i].ref_count++;

    add_child(parent, child);
    init_signals(child);

    add_new_task(child);
    // printf("Forked new task: child pid %d, parent pid %d\n", child->pid, parent->pid);
    return child->pid;
}

pid_t _fork(void)
{
    cpu_state_t state;
    capture_cpu_state(&state);
    return _do_fork(&state);
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

    sock = sys_connect("/foo/bar");
    if (sock < 0)
    {
        puts_color("Error creating socket\n", RED);
        return;
    }

    printf("sleeping for 5 seconds '%d'\n", get_kuptime());
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
    printf("Recursion %d\n", i++);
    recursion();
}

void test_recursion(void)
{
    recursion();
}

void task_read()
{
    printf("Task 4 Started\n");
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

    printf(fd_str, fd);

    sys_write(fd, buffer, 10);
    sys_close(fd);

    memset(buffer, 0, sizeof(buffer));
    // while (1)
    // {
        // fd = open("/boot/task_read", O_RDONLY);
        // printf("fd------: %d\n", fd);
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
        sys_close(fd);
    }

}

/* It needs to get a new page directory if i would want it to work again. */
void create_user_code_task(char *name)
{
    uint8_t *code_page = vmalloc(PAGE_SIZE);
    uint8_t *data_page = vmalloc(PAGE_SIZE);

    uint32_t code_pa = vmm_get_physical(vmm_current_directory(), (uint32_t)code_page);
    uint32_t data_pa = vmm_get_physical(vmm_current_directory(), (uint32_t)data_page);

    vmm_unmap_page(vmm_current_directory(), (uint32_t)code_page);
    vmm_unmap_page(vmm_current_directory(), (uint32_t)data_page);
    vmm_map_page(vmm_current_directory(), (uint32_t)code_page, code_pa, PAGE_USER_RW);
    vmm_map_page(vmm_current_directory(), (uint32_t)data_page, data_pa, PAGE_USER_RW);

    uint32_t *dir_entries = (uint32_t *)vmm_current_directory();
    uint32_t dir_idx = (uint32_t)code_page >> 22;
    uint32_t tbl_phys = dir_entries[dir_idx] & 0xFFFFF000;
    uint32_t *tbl = (uint32_t *)tbl_phys;
    uint32_t tbl_idx = ((uint32_t)code_page >> 12) & 0x3FF;
    uint32_t pde_flags = dir_entries[dir_idx] & 0xFFF;
    printf("PDE flags for code_page: %x\n", pde_flags);
    printf("PTE flags for code_page: %x\n", tbl[tbl_idx] & 0xFFF);

    memcpy(data_page, user_msg, sizeof(user_msg));
    memcpy(code_page, user_code, sizeof(user_code));


    printf("code bytes at %p: ", code_page);
    for (int i = 0; i < 10; i++)
    {
        put_2_hex(code_page[i]);
        putc(' ');
    }
    printf("\n");
    *(uint32_t *)(code_page + 11) = (uint32_t)data_page;

    printf("ecx patch at offset 11: %x\n", *(uint32_t*)(code_page + 11));

    create_user_task_at((uint32_t)code_page, name, NULL, NULL);
}

void create_user_task_at(uint32_t entry_addr, char *name, void (*on_exit)(void), page_directory_t* _task_dir)
{
    task_t *task = kmalloc(sizeof(task_t));
    memset(task, 0, sizeof(task_t));

    uint32_t *kernel_stack_base = kmalloc(STACK_SIZE);
    uint32_t *kernel_stack_top  = kernel_stack_base + STACK_SIZE / sizeof(uint32_t);

    task->page_dir = _task_dir;

    page_directory_t *task_dir;
    if (_task_dir)
        task_dir = _task_dir;
    else
        task_dir = vmm_current_directory();

    uint32_t *user_stack_base = vmalloc(USER_STACK_SIZE);
    uint32_t user_pages = PAGE_ALIGN(USER_STACK_SIZE) / PAGE_SIZE;
    for (uint32_t i = 0; i < user_pages; i++)
    {
        uint32_t va = (uint32_t)user_stack_base + i * PAGE_SIZE;
        uint32_t pa = vmm_get_physical(task_dir, va);
        vmm_unmap_page(task_dir, va);
        vmm_map_page(task_dir, va, pa, PAGE_USER_RW);
    }

    /* Create exit stub for enforcing all programs to exit cleanly */
    uint8_t *stub_page = vmalloc(PAGE_SIZE);
    uint32_t stub_pa = vmm_get_physical(task_dir, (uint32_t)stub_page);
    vmm_unmap_page(task_dir, (uint32_t)stub_page);
    vmm_map_page(task_dir, (uint32_t)stub_page, stub_pa, PAGE_USER_RW);

    uint32_t *dir = (uint32_t*)task_dir;
    dir[(uint32_t)stub_page >> 22] |= PAGE_USER;
    __asm__ __volatile__("invlpg (%0)" : : "r"((uint32_t)stub_page) : "memory");

    memcpy(stub_page, exit_stub, sizeof(exit_stub));

    task->stub_page = (uintptr_t)stub_page;

    uint32_t *usp = user_stack_base + USER_STACK_SIZE / sizeof(uint32_t);
    *--usp = (uint32_t)stub_page;

    uint32_t *ksp = kernel_stack_top;
    *--ksp = 0x2B; /* SS */
    *--ksp = (uint32_t)usp; /* ESP */
    *--ksp = 0x202; /* EFLAGS */
    *--ksp = 0x23; /* CS ring 3*/
    *--ksp = entry_addr; /* EIP */

    for (uint32_t i = 0; i < 8; i++)
        *--ksp = 0;

    task->cpu.esp_     = (uint32_t)ksp;
    task->kernel_stack = (uintptr_t)kernel_stack_top;
    task->kernel_stack_base = (uintptr_t)kernel_stack_base;
    task->stack        = (uintptr_t)user_stack_base;
    task->pid          = task_index++;
    task->state        = TASK_READY;
    task->is_user      = true;
    task->on_exit      = on_exit;
    task->uid = 1000; task->euid = 1000; task->gid = 1000;
    task->screen_echo  = true;
    memcpy(task->name, name, strlen(name) > 15 ? 15 : strlen(name));
    task->name[15] = '\0';
    memset(task->fd_table, 0, sizeof(task->fd_table));
    init_standard_fds(task);
    init_signals(task);

    add_new_task(task);
}

void task_wait()
{
    // printf("Task 5 Started\n");
    pid_t pid;
    int status;
    while (1)
    {
        pid = _wait(&status);
        (void)pid;
        set_putchar_color(GREEN);
        // printf("Task 5: Child %d exited with status %d\n", pid, status);
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
    create_task(kshell, "kshell", NULL);
    create_task(task_wait, "task_wait", NULL);
    create_task(task_1, "task_1", task_1_exit);
    create_task(task_read, "task_read", NULL);
    create_task(socket_1, "socket_1", NULL);
    create_task(socket_2, "socket_2", NULL);
    // create_user_code_task("user_code_task");
    exec_bin("/hello");
    exec_bin("/hello2");
    // exec_bin("/ushell");

    to_free = NULL;
    // printf("current_task: %p\n", current_task);
}

/* ################################################################### */
/*                               TESTS                                 */
/* ################################################################### */
void show_tasks()
{
    task_t *current = task_list;
    do
    {
        printf("Task '%s'\n", current->name);
        printf("  PID: %d\n", current->pid);
        printf("  ESP: %p\n", current->cpu.esp_);
        printf("  EIP: %p\n", current->cpu.eip);
        printf("  State: %d\n", current->state);
        current = current->next;
    } while (current != task_list);
}
