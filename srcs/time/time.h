#ifndef TIME_H
#define TIME_H

#include "../utils/stdint.h"

typedef struct
{
    uint32_t tv_sec;
    uint32_t tv_nsec;
} timespec_t;


void init_timer();
void irq_handler_timer();
void sleep(uint32_t seconds);
uint64_t get_kuptime();

void init_time();

void sleep(uint32_t seconds);

uint64_t get_uptime();

void get_system_time(timespec_t* ts);
void set_system_time(const timespec_t* ts);

uint32_t get_tick_count();

#endif
