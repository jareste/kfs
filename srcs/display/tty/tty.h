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
} tty_device_t;

void tty_delete_ch(tty_device_t *tty);
int tty_keyboard_input(void *fp, const char *buf, size_t count);
int open_tty_device(void *tty_device, file_t* f);
#endif