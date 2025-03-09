#include "../utils/stdint.h"
#include "../io/io.h"
#include "../keyboard/idt.h"
#include "../tasks/task.h"
#include "time.h"

/* PIT and PIC definitions */
#define PIT_CONTROL_PORT     0x43
#define PIT_CHANNEL0_PORT    0x40
#define PIT_BASE_FREQUENCY   1193182
#define PIT_FREQUENCY        100
#define PIC1_COMMAND         0x20
#define PIC_EOI              0x20
#define SECONDS_TO_TICKS(x)  ((x) * PIT_FREQUENCY)

/* Global timekeeping variables */
static uint32_t tick_count = 0;
static uint64_t seconds = 0;

/* Helper function: Convert BCD (Binary-Coded Decimal) to binary */
static uint8_t bcd_to_bin(uint8_t bcd)
{
    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

uint8_t rtc_read(uint8_t reg)
{
    outb(0x70, reg);
    return inb(0x71);
}

static uint32_t rtc_to_epoch(int year, int month, int day, int hour, int minute, int second) 
{
    int a;
    int b;
    int jdn;
    int days_since_epoch;
    uint32_t epoch;

    if (month <= 2)
    {
        year -= 1;
        month += 12;
    }
    a = year / 100;
    b = 2 - a + (a / 4);

    jdn = (int)(365.25 * (year + 4716)) + (int)(30.6001 * (month + 1)) + day + b - 1524;

    days_since_epoch = jdn - 2440588;
    epoch = days_since_epoch * 86400 + hour * 3600 + minute * 60 + second;
    return epoch;
}

static void call_rtc(int* full_year, int* month, int* day, int* hour, int* minute, int* second)
{
    uint8_t sec_bcd;
    uint8_t min_bcd;
    uint8_t hour_bcd;
    uint8_t day_bcd;
    uint8_t month_bcd;
    uint8_t year_bcd;
    int year;

    sec_bcd = rtc_read(0x00);
    min_bcd = rtc_read(0x02);
    hour_bcd = rtc_read(0x04);
    day_bcd = rtc_read(0x07);
    month_bcd = rtc_read(0x08);
    year_bcd = rtc_read(0x09);

    *second = bcd_to_bin(sec_bcd);
    *minute = bcd_to_bin(min_bcd);
    *hour = bcd_to_bin(hour_bcd);
    *day = bcd_to_bin(day_bcd);
    *month = bcd_to_bin(month_bcd);
    year = bcd_to_bin(year_bcd);

    if (year < 70)
        *full_year = 2000 + year;
    else
        *full_year = 1900 + year;
}

void get_system_time(timespec_t* ts)
{
    int second;
    int minute;
    int hour;
    int day;
    int month;
    int full_year;

    call_rtc(&full_year, &month, &day, &hour, &minute, &second);

    ts->tv_sec = rtc_to_epoch(full_year, month, day, hour, minute, second);
    ts->tv_nsec = 0;
}

time_t time(time_t* tloc)
{
    timespec_t ts;
    get_system_time(&ts);
    if (tloc)
        *tloc = ts.tv_sec;
    return ts.tv_sec;
}

void print_date()
{
    timespec_t ts;
    int second;
    int minute;
    int hour;
    int day;
    int month;
    int full_year;

    call_rtc(&full_year, &month, &day, &hour, &minute, &second);

    printf("Current date: %d-%d-%d %d:%d:%d\n", full_year, month, day, hour, minute, second);
    ts.tv_sec = rtc_to_epoch(full_year, month, day, hour, minute, second);
    ts.tv_nsec = 0;
    dispatch_time_request(&ts);
}

void sleep(uint32_t sleep_seconds)
{
    uint64_t wake_tick = tick_count + SECONDS_TO_TICKS(sleep_seconds);
    schedule_task_sleep(get_current_task(), wake_tick);
}

void usleep(uint32_t sleep_microseconds)
{
    uint64_t wake_tick = tick_count + sleep_microseconds / 1000;
    schedule_task_sleep(get_current_task(), wake_tick);
}

void irq_handler_timer()
{
    timespec_t ts;

    tick_count++;
    if ((tick_count % PIT_FREQUENCY) == 0)
    {
        seconds++;

        get_system_time(&ts);
        dispatch_time_request(&ts);
    }
    dispatch_cpu_cycle();
    outb(PIC_EOI, PIC1_COMMAND);
}

uint32_t get_tick_count()
{
    return tick_count;
}

uint64_t get_kuptime()
{
    return seconds;
}

void init_pit(uint32_t frequency)
{
    if (frequency < 18)
    {
        frequency = 18;
    }
    else if (frequency > PIT_BASE_FREQUENCY)
    {
        frequency = PIT_BASE_FREQUENCY;
    }
    uint16_t divisor = PIT_BASE_FREQUENCY / frequency;
    outb(PIT_CONTROL_PORT, 0x36);
    outb(PIT_CHANNEL0_PORT, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0_PORT, (uint8_t)((divisor >> 8) & 0xFF));
}

void init_timer()
{
    init_pit(PIT_FREQUENCY);
}
