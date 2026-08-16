#ifndef SIGNALS_H
#define SIGNALS_H

#include "../utils/stdint.h"

#define MAX_SIGNALS 32

/* Standard-ish signal numbers used by the tty/keyboard layer. */
#define SIGINT  2
#define SIGQUIT 3

typedef void (*signal_handler_t)(int);
typedef int pid_t;

typedef struct
{
    signal_handler_t handlers[MAX_SIGNALS]; /* Handler for each signal, _signal() would set it. */
    uint32_t pending_signals; /* 'flag' of pending signals */
    uint32_t blocked_signals; /* 'flag' of blocked signals */
} signal_context_t;

int _signal(int signal, signal_handler_t handler);
int _kill(pid_t pid, int signal);

void handle_signals(void *frame);
void init_signals();

int _sigaction(int signal, int set_handler, void *raw_handler, void **old_raw_handler);

#endif