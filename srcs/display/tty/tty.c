#include "tty.h"
#include "../../tasks/task.h"

/* ------------------------------------------------------------------ */
/*  ANSI/VT100 SGR (color) filter                              */
/* ------------------------------------------------------------------ */
static enum { ANSI_NORMAL, ANSI_ESC, ANSI_CSI } ansi_state = ANSI_NORMAL;
static char ansi_params[32];
static int ansi_param_len = 0;

static uint8_t ansi_to_vga_fg(int code, int bold)
{
    static const uint8_t map[8] = {
        BLACK, RED, GREEN, BROWN /* "yellow" */, BLUE, MAGENTA, CYAN, LIGHT_GREY
    };
    uint8_t base = map[code & 0x7];
    if (bold && base < 8)
        base += 8; /* bright variant: BLACK->DARK_GREY, RED->LIGHT_RED, ... */
    return base;
}

static void ansi_apply_sgr(void)
{
    static int bold = 0;
    char *p = ansi_params;

    ansi_params[ansi_param_len] = '\0';
    if (ansi_param_len == 0)
    {
        bold = 0;
        set_putchar_color(LIGHT_GREY);
        return;
    }

    while (*p)
    {
        int val = 0;
        while (*p >= '0' && *p <= '9')
        {
            val = val * 10 + (*p - '0');
            p++;
        }
        if (*p == ';')
            p++;

        if (val == 0)
        {
            bold = 0;
            set_putchar_color(LIGHT_GREY);
        }
        else if (val == 1)
            bold = 1;
        else if (val >= 30 && val <= 37)
            set_putchar_color(ansi_to_vga_fg(val - 30, bold));
        else if (val == 39)
            set_putchar_color(bold ? DARK_GREY : LIGHT_GREY);
        else if (val >= 90 && val <= 97)
            set_putchar_color(ansi_to_vga_fg(val - 90, 1));
    }
}

static bool ansi_filter(char c)
{
    switch (ansi_state)
    {
        case ANSI_NORMAL:
            if (c == '\033')
            {
                ansi_state = ANSI_ESC;
                return true;
            }
            return false;

        case ANSI_ESC:
            if (c == '[')
            {
                ansi_state = ANSI_CSI;
                ansi_param_len = 0;
            }
            else
            {
                ansi_state = ANSI_NORMAL; /* not a CSI sequence, drop it */
            }
            return true;

        case ANSI_CSI:
            if ((c >= '0' && c <= '9') || c == ';')
            {
                if (ansi_param_len < (int)sizeof(ansi_params) - 1)
                    ansi_params[ansi_param_len++] = c;
                return true;
            }
            /* Any other byte is the final byte, terminating the sequence. */
            if (c == 'm')
                ansi_apply_sgr();
            ansi_state = ANSI_NORMAL;
            return true;
    }
    return false;
}

void tty_write_ch(tty_device_t *tty, char c)
{
    tty->buffer[tty->write_pos] = c;
    tty->write_pos = (tty->write_pos + 1) % TTY_BUFFER_SIZE;
    putc(c); /* to be removed */
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

void _tty_write(tty_device_t* tty, const char* str)
{
    size_t i;

    for (i = 0; i < (size_t)strlen(str); i++)
    {
        tty->buffer[tty->write_pos] = str[i];
        tty->write_pos = (tty->write_pos + 1) % TTY_BUFFER_SIZE;
    }
}

char _tty_read(tty_device_t *tty)
{
    char c;

    if (tty->read_pos == tty->write_pos)
    {
        return '\0';
    }

    c = tty->buffer[tty->read_pos];
    tty->read_pos = (tty->read_pos + 1) % TTY_BUFFER_SIZE;
    return c;
}

ssize_t tty_read(void *fp, void *buf_, size_t count)
{
    tty_device_t* tty = (tty_device_t *)fp;
    char* buf = (char *)buf_;
    char c;
    size_t i = 0;
    int has_line_end;
    uint32_t pos;
    task_t* self;

    /* Block until a complete line is available */
    kprintf("");
    while (1)
    {
        /* Check if a signal is pending, so in that scenario we'd return -EINTR 
         * in order to come back to ring 3 and handle the signal over there. 
         */
        self = get_current_task();
        if (self->signals.pending_signals & ~self->signals.blocked_signals)
            return -4; // -EINTR

        /* Search for a '\n' or EOF (Ctrl+D, 0x04) in the buffer */
        has_line_end = 0;
        pos = tty->read_pos;
        while (pos != tty->write_pos)
        {
            if (tty->buffer[pos] == '\n' || tty->buffer[pos] == 0x04)
            {
                has_line_end = 1;
                break;
            }
            pos = (pos + 1) % TTY_BUFFER_SIZE;
        }

        if (has_line_end)
            break;
            
        /* No data - mark it as sleeping to avoid busy-waiting */
        get_current_task()->state = TASK_SLEEPING;
        get_current_task()->wake_tick = get_tick_count() + 1;
        while (get_current_task()->state == TASK_SLEEPING)
        {
            enable_interrupts();
            ; /* Scheduler will wake it up */
        }
    }

    /* Read until '\n' or EOF */
    while (i < count)
    {
        c = _tty_read(tty);
        if (c == '\0')
            break;
        if (c == 0x04)
        {
            break;
        }
        if (c == '\b')
        {
            if (i > 0) i--;
            buf[i] = '\0';
            if (get_current_task()->screen_echo)
                putc(c);
            continue;
        }
        if (get_current_task()->screen_echo)
            putc(c);
        buf[i++] = c;
        if (c == '\n')
            break;
    }

    return i;
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

void clear_tty_buffer(tty_device_t *tty)
{
    memset(tty->buffer, 0, TTY_BUFFER_SIZE);
    tty->read_pos = 0;
    tty->write_pos = 0;
}

ssize_t tty_write(void *fp, const void *buf_, size_t count)
{
    const char *buf = (const char *)buf_;
    (void)fp;
    for (size_t i = 0; i < count; i++)
    {
        if (!ansi_filter(buf[i]))
            putc(buf[i]);  // directo a pantalla, no al buffer
    }
    return count;
}

int tty_keyboard_input(void *fp, const char *buf, size_t count)
{
    tty_device_t *tty = (tty_device_t *)fp;
    for (size_t i = 0; i < count; i++) {
        tty->buffer[tty->write_pos] = buf[i];
        tty->write_pos = (tty->write_pos + 1) % TTY_BUFFER_SIZE;
        putc(buf[i]);  // echo del teclado
    }
    return count;
}

int tty_close(void *fp)
{
    tty_device_t *tty = (tty_device_t *)fp;
    tty->ref_count--;

    if (tty->ref_count == 0)
        kfree(tty);

    return 0;
}

int open_tty_device(void *tty_device, file_t* f)
{
    tty_device_t *tty = (tty_device_t *)tty_device;
    tty->ref_count++;

    f->fops.write = tty_write;
    f->fops.read = tty_read;
    f->fops.close = tty_close;
    f->ref_count = 1;
    f->offset = 0;
    f->type = FD_TTY;
    f->fp = tty_device;
    return 1;
}
