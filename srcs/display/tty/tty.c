#include "tty.h"

static tty_device_t tty;

void tty_write_ch(char c)
{
    tty.buffer[tty.write_pos] = c;
    tty.write_pos = (tty.write_pos + 1) % TTY_BUFFER_SIZE;
}

void tty_write(const char *str)
{
    for (size_t i = 0; i < strlen(str); i++)
    {
        tty.buffer[tty.write_pos] = str[i];
        tty.write_pos = (tty.write_pos + 1) % TTY_BUFFER_SIZE;
    }
}

char tty_read()
{
    if (tty.read_pos == tty.write_pos)
    {
        return 0;
    }

    char c = tty.buffer[tty.read_pos];
    tty.read_pos = (tty.read_pos + 1) % TTY_BUFFER_SIZE;
    return c;
}

int tty_save_to_file(const char *path)
{
    ext2_FILE *file = ext2_fopen(path, "w");
    if (!file)
    {
        return -1;
    }

    for (uint32_t i = 0; i < TTY_BUFFER_SIZE; i++)
    {
        if (tty.buffer[i] == 0)
        {
            break;
        }
        ext2_fwrite(file, &tty.buffer[i], 1);
    }

    ext2_fclose(file);
    return 0;
}

void tty_init()
{
    tty.read_pos = 0;
    tty.write_pos = 0;
    tty.ready = true;
}
