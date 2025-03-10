#ifndef TIME_H
#define TIME_H

#include "../utils/stdint.h"

typedef struct
{
    uint32_t tv_sec;
    uint32_t tv_nsec;
} timespec_t;

typedef struct tm
{
    uint32_t tm_sec;   /* seconds */
    uint32_t tm_min;   /* minutes */
    uint32_t tm_hour;  /* hours */
    uint32_t tm_mday;  /* day of the month */
    uint32_t tm_mon;   /* month */
    uint32_t tm_year;  /* year */
    uint32_t tm_wday;  /* day of the week */
    uint32_t tm_yday;  /* day in the year */
    uint32_t tm_isdst; /* daylight saving time */
};

void init_timer();
void irq_handler_timer();
// void sleep(uint32_t seconds);
uint64_t get_kuptime();

void init_time();

void _usleep(uint32_t sleep_microseconds);
void _sleep(uint32_t seconds);

uint64_t get_uptime();

void get_system_time(timespec_t* ts);
void set_system_time(const timespec_t* ts);

time_t _time(time_t* tloc);

uint32_t get_tick_count();

#endif
