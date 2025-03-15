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
} tty_device_t;

void tty_delete_ch(tty_device_t *tty);
#endif