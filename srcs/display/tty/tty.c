#include "tty.h"
#include "../../tasks/task.h"

// static tty_device_t tty;

void tty_write_ch(tty_device_t *tty, char c)
{
    tty->buffer[tty->write_pos] = c;
    tty->write_pos = (tty->write_pos + 1) % TTY_BUFFER_SIZE;
}

void tty_delete_ch(tty_device_t *tty)
{
    if (tty->write_pos == 1)
    {
        tty->write_pos--;
        tty->buffer[tty->write_pos] = 0;
    
        return;
    }
    /* Here we are deleting the '\b' we wrote. */
    tty->write_pos--;
    tty->buffer[tty->write_pos] = 0;
    /* and here the actual letter to remove. */
    tty->write_pos--;
    tty->buffer[tty->write_pos] = 0;
    if (tty->read_pos > tty->write_pos)
    {
        tty->read_pos = tty->write_pos;
    }
}

void _tty_write(tty_device_t *tty, const char *str)
{
    for (size_t i = 0; i < strlen(str); i++)
    {
        tty->buffer[tty->write_pos] = str[i];
        tty->write_pos = (tty->write_pos + 1) % TTY_BUFFER_SIZE;
    }
}

char _tty_read(tty_device_t *tty)
{
    if (tty->read_pos == tty->write_pos)
    {
        return '\0';
    }

    char c = tty->buffer[tty->read_pos];
    if (c == '\b')
    {
        tty_delete_ch(tty);
        return c;
    }
    tty->read_pos = (tty->read_pos + 1) % TTY_BUFFER_SIZE;
    return c;
}

int tty_save_to_file(tty_device_t* tty,const char *path)
{
    ext2_FILE *file = ext2_fopen(path, "w");
    if (!file)
    {
        return -1;
    }

    for (uint32_t i = 0; i < TTY_BUFFER_SIZE; i++)
    {
        if (tty->buffer[i] == 0)
        {
            break;
        }
        ext2_fwrite(file, &tty->buffer[i], 1);
    }

    ext2_fclose(file);
    return 0;
}

// void tty_init()
// {
//     tty.read_pos = 0;
//     tty.write_pos = 0;
//     tty.ready = true;
// }

void clear_tty_buffer(tty_device_t *tty)
{
    memset(tty->buffer, 0, TTY_BUFFER_SIZE);
    tty->read_pos = 0;
    tty->write_pos = 0;
}

int tty_read(void *fp, char *buf, size_t count)
{
    tty_device_t *tty = (tty_device_t *)fp;
    size_t i;
    char c;

    while (1)
    {
        c = _tty_read(tty);
        if (c == '\0')
        {
            break;
        }
        if (get_current_task()->screen_echo == true)
        {
            putc(c);
        }
    }

    clear_tty_buffer(tty);
    i = 0;
    while (1)
    {
        c = _tty_read(tty);
        if (c == '\0')
        {
            scheduler();
            continue;
        }
        if (c == '\b')
        {
            if (i > 0)
            {
                i--;
            }
            buf[i] = '\0';
            continue;
        }
        if (get_current_task()->screen_echo == true)
        {
            putc(c);
        }
        /* if '\n' must be into buff just move it down */
        if (c == '\n' || i == count - 1)
        {
            i++;
            break;
        }
        
        buf[i] = c;
        i++;
    }
    // for (i = 0; i < count; i++)
    // {
    //     buf[i] = _tty_read(tty);
    //     if (buf[i] == '\n')
    //     {
    //         break;
    //     }
    //     // tty->read_pos++;
    // }
    return i;
}

int tty_write(void *fp, const char *buf, size_t count)
{
    tty_device_t *tty = (tty_device_t *)fp;

    size_t i;
    for (i = 0; i < count; i++)
    {
        tty_write_ch(tty, buf[i]);
    }
    return i;
}

int tty_close(void *fp)
{
    tty_device_t *tty = (tty_device_t *)fp;

    kfree(tty);
    return 0;
}

int open_tty_device(void *tty_device, file_t* f)
{
    f->fops.write = tty_write;
    f->fops.read = tty_read;
    f->fops.close = tty_close;
    f->ref_count = 1;
    f->offset = 0;
    f->type = FD_TTY;
    f->fp = tty_device;
    return 1;
}
