#include "signals.h"
#include "../utils/utils.h"
#include "../display/display.h"
#include "../tasks/task.h"
#include "../panic/kpanic.h"
#include "idt.h"

#define SIG_DFL_RAW ((void*)0)
#define SIG_IGN_RAW ((void*)1)

static void signal_handler(int signal);
static void sig_ignore_handler(int signal);

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
    // kprintf("SIGNAL################task: %p\n", task);
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

static bool deliver_to_userspace(task_t *task, iret_regs_t *frame, int signal, signal_handler_t handler)
{
    uint32_t usp;

    if (!task->is_user || !frame || frame->cs != 0x23 || task->sig_frame_valid)
        return false;

    task->saved_sig_frame = *frame;
    task->sig_frame_valid = true;

    usp = frame->esp_user - 8;
    *(uint32_t*)(usp + 4) = (uint32_t)signal; /* handler's argument */
    *(uint32_t*)(usp + 0) = KFS_SIGRETURN_STUB_ADDR; /* its "return address" */

    frame->eip = (uint32_t)handler;
    frame->esp_user = usp;
    return true;
}

void handle_signals(void *frame_)
{
    task_t *task = get_current_task();
    iret_regs_t *frame = (iret_regs_t*)frame_;
    signal_handler_t h;
    int i;

    if (task->signals.pending_signals == 0)
        return;

    for (i = 0; i < MAX_SIGNALS; i++)
    {
        if (!(task->signals.pending_signals & (1 << i)) ||
            (task->signals.blocked_signals & (1 << i)))
            continue;

        h = task->signals.handlers[i];
        if (!h)
        {
            task->signals.pending_signals &= ~(1 << i);
            continue;
        }

        /* Non-user tasks (kshell, kernel threads) only ever install
         * kernel-space handlers of their own (e.g. kshell_sigint_handler)
         * -- always safe to call directly. Same for our own two stand-ins
         * (see _sigaction()) even on a user task, since they're kernel
         * code too. Anything else installed on a user task is a real
         * userspace pointer and needs the redirect above. */
        if (task->is_user && h != signal_handler && h != sig_ignore_handler)
        {
            if (deliver_to_userspace(task, frame, i, h))
            {
                task->signals.pending_signals &= ~(1 << i);
                return; /* frame is now committed to the handler */
            }
            /* Already mid-handler (sig_frame_valid) or not a user task
             * with a live frame to redirect: leave it pending rather
             * than mis-deliver it, we'll retry next tick. */
            continue;
        }

        task->signals.pending_signals &= ~(1 << i);
        h(i);
    }
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
        kprintf("Task with PID %d not found\n", pid);
        return -1;
    }
    if (signal >= 0 && signal < MAX_SIGNALS)
    {
        kprintf("Sending signal %d to PID %d\n", signal, pid);
        task->signals.pending_signals |= (1 << signal);
    }
    return 1;
}

static void signal_handler(int signal)
{
    kill_task(signal);
}

static void sig_ignore_handler(int signal)
{
    (void)signal;
}

static void *handler_to_raw(signal_handler_t h)
{
    if (h == signal_handler)
        return SIG_DFL_RAW;
    if (h == sig_ignore_handler || h == NULL)
        return SIG_IGN_RAW;
    return (void*)h;
}

int _sigaction(int signal, int set_handler, void *raw_handler, void **old_raw_handler)
{
    task_t *task = get_current_task();

    if (signal < 0 || signal >= MAX_SIGNALS)
        return -1;

    if (old_raw_handler)
        *old_raw_handler = handler_to_raw(task->signals.handlers[signal]);

    if (!set_handler)
        return 0;

    if (raw_handler == SIG_DFL_RAW)
        task->signals.handlers[signal] = signal_handler;
    else if (raw_handler == SIG_IGN_RAW)
        task->signals.handlers[signal] = sig_ignore_handler;
    else
        task->signals.handlers[signal] = (signal_handler_t)raw_handler;

    return 0;
}

static void panic_signal_handler(int signal)
{
    (void)signal;
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

    // kprintf("Signal handlers set for PID %d\n", task->pid);
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
