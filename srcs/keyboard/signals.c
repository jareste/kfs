#include "signals.h"
#include "../utils/utils.h"
#include "../display/display.h"
#include "../tasks/task.h"
#include "../panic/kpanic.h"
#include "idt.h"

static void signal_handler(int signal);

void signal_task(task_t* task, int signal, signal_handler_t handler)
{
    if (signal >= 0 && signal < MAX_SIGNALS)
    {
        task->signals.handlers[signal] = handler;
    }
}

int _signal(int signal, signal_handler_t handler)
{
    task_t *task = get_current_task();
    // printf("SIGNAL################task: %p\n", task);
    task->signals.handlers[signal] = handler;
    return 1;
}

void block_signal(int signal)
{
    task_t *task = get_current_task();
    if (signal >= 0 && signal < MAX_SIGNALS)
    {
        task->signals.blocked_signals |= (1 << signal);
    }
}

void unblock_signal(int signal)
{
    task_t *task = get_current_task();
    if (signal >= 0 && signal < MAX_SIGNALS)
    {
        task->signals.blocked_signals &= ~(1 << signal);
    }
}

void handle_signals()
{
    task_t *task = get_current_task();
    // printf("Handling signals for PID %d, pending: %d\n", task->pid,task->signals.pending_signals);
    // printf("pid: %d\n", task->pid);
    // printf("Handling signals for PID %p\n", task->signals);
    if (task->pid == 9 && task->signals.pending_signals != 0)
    {
        printf("Handling signals for PID %d, pending: %d\n", task->pid,task->signals.pending_signals);
    }

    if (task->signals.pending_signals == 0)
    {
    // printf("end of handling signals\n");
        return;
    }
    for (int i = 0; i < MAX_SIGNALS; i++)
    {
        if ((task->signals.pending_signals & (1 << i)) &&\
         !(task->signals.blocked_signals & (1 << i)))
        {
            task->signals.pending_signals &= ~(1 << i);
            if (task->signals.handlers[i])
            {
                task->signals.handlers[i](i);
            }
        }
    }
    // enable_interrupts();
    // outb(0x20, 0x20);
}

int _kill(pid_t pid, int signal)
{
    if (pid == 0)
    {
        return -1;
    }
    task_t *task = get_task_by_pid(pid);
    if (!task)
    {
        printf("Task with PID %d not found\n", pid);
        return -1;
    }
    if (signal >= 0 && signal < MAX_SIGNALS)
    {
        printf("Sending signal %d to PID %d\n", signal, pid);
        task->signals.pending_signals |= (1 << signal);
    }
    return 1;
}

static void signal_handler(int signal)
{
    kill_task(signal);
}

static void panic_signal_handler(int signal)
{
    kpanic("Panic signal received", 1);
}

void init_signals(task_t* task)
{
    for (int i = 0; i < MAX_SIGNALS; i++)
    {
        task->signals.handlers[i] = NULL;
    }
    task->signals.pending_signals = 0;
    task->signals.blocked_signals = 0;

    for (int i = 0; i < MAX_SIGNALS; i++)
    {
        signal_task(task, i, signal_handler);
    }
    signal_task(task, 0, panic_signal_handler);
    signal_task(task, 6, panic_signal_handler);
    signal_task(task, 14, panic_signal_handler);

    // printf("Signal handlers set for PID %d\n", task->pid);
    // task->signals.handlers[6](6);
    task->signals.pending_signals = 0;
    task->signals.blocked_signals = 0;
}

/* This will come when i'll be having tasks. */
// void send_signal_to_task(task_t* task, int signal)
// {
//     if (signal >= 0 && signal < MAX_SIGNALS)
//     {
//         task->signals.pending_signals |= (1 << signal);
//     }
// }

// void task_signal_handler(task_t* task)
// {
//     for (int i = 0; i < MAX_SIGNALS; i++)
//     {
//         if ((task->signals.pending_signals & (1 << i)) && 
//             !(task->signals.blocked_signals & (1 << i)))
//             {
//             task->signals.pending_signals &= ~(1 << i);
//             if (task->signals.handlers[i])
//             {
//                 task->signals.handlers[i](i);
//             }
//         }
//     }
// }
