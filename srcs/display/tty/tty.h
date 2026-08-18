#ifndef TTY_H
#define TTY_H

#include "../../ide/ext2_fileio.h"
#include "../display.h"
#include "../../utils/stdint.h"

#define TTY_BUFFER_SIZE 1024

typedef struct
{
    char buffer[TTY_BUFFER_SIZE];
    uint32_t read_pos;
    uint32_t write_pos;
    bool ready;
    int ref_count;

    uint32_t line_start;
    uint32_t edit_pos;
} tty_device_t;

void tty_write_ch(tty_device_t *tty, char c);
void tty_delete_ch(tty_device_t *tty);
int tty_keyboard_input(void *fp, const char *buf, size_t count);
int open_tty_device(void *tty_device, file_t* f);

/* Line editing (arrow keys), used by keyboard.c for the foreground tty. */
void tty_mark_line_start(tty_device_t *tty);
void tty_insert_char(tty_device_t *tty, char c);
void tty_backspace_at_cursor(tty_device_t *tty);
void tty_delete_forward(tty_device_t *tty);
void tty_move_edit_cursor(tty_device_t *tty, int delta);
void tty_recall_line(tty_device_t *tty, const char *line, size_t len);
size_t tty_current_line(tty_device_t *tty, char *out, size_t out_size);

#endif