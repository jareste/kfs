#ifndef DISPLAY_H
#define DISPLAY_H

#include "../utils/utils.h"
#include "../io/io.h"

#define VIDEO_MEMORY 0xb8000
#define SCREEN_WIDTH 80
#define SCREEN_HEIGHT 25
#define MAX_CURSOR_POSITION SCREEN_WIDTH * SCREEN_HEIGHT

#define BANNER "Welcome to 42Barcelona's jareste- OS\n"

#define BLACK           0x00
#define BLUE            0x01
#define GREEN           0x02
#define CYAN            0x03
#define RED             0x04
#define MAGENTA         0x05
#define BROWN           0x06
#define LIGHT_GREY      0x07
#define DARK_GREY       0x08
#define LIGHT_BLUE      0x09
#define LIGHT_GREEN     0x0A
#define LIGHT_CYAN      0x0B
#define LIGHT_RED       0x0C
#define LIGHT_MAGENTA   0x0D
#define LIGHT_BROWN     0x0E
#define WHITE           0x0F

void set_putchar_color(uint8_t c);

void putc(char c);
void putc_color(char c, uint8_t color);

int puts(const char *str);
int puts_color(const char *str, uint8_t color);

void kdump(void* addr, uint32_t size);
int put_hex(uint32_t n);
void put_zu(size_t value);

int printf(const char *format, ...);


void clear_screen();
void delete_last_char();
void delete_until_char();
void move_cursor_left();
void move_cursor_right();
void move_cursor_up();
void move_cursor_down();
void delete_actual_char();

void start_ofuscation();
void stop_ofuscation();

void enable_print();
void disable_print();

int write_stdout_wrapper(int fd, const char *buf, size_t count);
int write_stderr_wrapper(int fd, const char *buf, size_t count);

#endif
