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

void tty_mark_line_start(tty_device_t *tty)
{
    tty->line_start = tty->write_pos;
    tty->edit_pos = tty->write_pos;
}

static uint32_t tty_line_len(tty_device_t *tty)
{
    return (tty->write_pos - tty->line_start + TTY_BUFFER_SIZE) % TTY_BUFFER_SIZE;
}

static void tty_redraw_tail(tty_device_t *tty, uint32_t from_pos, uint32_t trailing_blanks, uint32_t cursor_target)
{
    int base_col = get_input_line_start();
    uint32_t offset;
    uint32_t pos;

    offset = (from_pos - tty->line_start + TTY_BUFFER_SIZE) % TTY_BUFFER_SIZE;
    set_cursor_position(base_col + (int)offset);

    for (pos = from_pos; pos != tty->write_pos; pos = (pos + 1) % TTY_BUFFER_SIZE)
        putc(tty->buffer[pos]);
    while (trailing_blanks--)
        putc(' ');

    offset = (cursor_target - tty->line_start + TTY_BUFFER_SIZE) % TTY_BUFFER_SIZE;
    set_cursor_position(base_col + (int)offset);
}

void tty_insert_char(tty_device_t *tty, char c)
{
    uint32_t old_edit_pos = tty->edit_pos;
    uint32_t pos;
    uint32_t prev;

    if (old_edit_pos == tty->write_pos)
    {
        tty_write_ch(tty, c);
        tty->edit_pos = tty->write_pos;
        return;
    }

    pos = tty->write_pos;
    while (pos != old_edit_pos)
    {
        prev = (pos - 1 + TTY_BUFFER_SIZE) % TTY_BUFFER_SIZE;
        tty->buffer[pos] = tty->buffer[prev];
        pos = prev;
    }
    tty->buffer[old_edit_pos] = c;
    tty->write_pos = (tty->write_pos + 1) % TTY_BUFFER_SIZE;
    tty->edit_pos = (old_edit_pos + 1) % TTY_BUFFER_SIZE;

    tty_redraw_tail(tty, old_edit_pos, 0, tty->edit_pos);
}

void tty_backspace_at_cursor(tty_device_t *tty)
{
    uint32_t del_pos;
    uint32_t src;
    uint32_t dst;

    if (tty->edit_pos == tty->line_start)
        return;

    del_pos = (tty->edit_pos - 1 + TTY_BUFFER_SIZE) % TTY_BUFFER_SIZE;

    dst = del_pos;
    src = tty->edit_pos;
    while (src != tty->write_pos)
    {
        tty->buffer[dst] = tty->buffer[src];
        dst = (dst + 1) % TTY_BUFFER_SIZE;
        src = (src + 1) % TTY_BUFFER_SIZE;
    }
    tty->write_pos = (tty->write_pos - 1 + TTY_BUFFER_SIZE) % TTY_BUFFER_SIZE;
    tty->edit_pos = del_pos;

    tty_redraw_tail(tty, del_pos, 1, tty->edit_pos);
}

void tty_delete_forward(tty_device_t *tty)
{
    uint32_t src;
    uint32_t dst;

    if (tty->edit_pos == tty->write_pos)
        return; /* nothing after the cursor */

    dst = tty->edit_pos;
    src = (tty->edit_pos + 1) % TTY_BUFFER_SIZE;
    while (src != tty->write_pos)
    {
        tty->buffer[dst] = tty->buffer[src];
        dst = (dst + 1) % TTY_BUFFER_SIZE;
        src = (src + 1) % TTY_BUFFER_SIZE;
    }
    tty->write_pos = (tty->write_pos - 1 + TTY_BUFFER_SIZE) % TTY_BUFFER_SIZE;

    tty_redraw_tail(tty, tty->edit_pos, 1, tty->edit_pos);
}

void tty_move_edit_cursor(tty_device_t *tty, int delta)
{
    uint32_t line_len = tty_line_len(tty);
    uint32_t cur_offset = (tty->edit_pos - tty->line_start + TTY_BUFFER_SIZE) % TTY_BUFFER_SIZE;
    int new_offset = (int)cur_offset + delta;

    if (new_offset < 0)
        new_offset = 0;
    if (new_offset > (int)line_len)
        new_offset = (int)line_len;

    tty->edit_pos = (tty->line_start + (uint32_t)new_offset) % TTY_BUFFER_SIZE;
    set_cursor_position(get_input_line_start() + new_offset);
}

void tty_recall_line(tty_device_t *tty, const char *line, size_t len)
{
    uint32_t old_len = tty_line_len(tty);
    uint32_t pos;
    uint32_t blanks;
    size_t i;

    tty->write_pos = tty->line_start;
    for (i = 0; i < len && i < TTY_BUFFER_SIZE - 1; i++)
    {
        tty->buffer[tty->write_pos] = line[i];
        tty->write_pos = (tty->write_pos + 1) % TTY_BUFFER_SIZE;
    }
    tty->edit_pos = tty->write_pos;

    set_cursor_position(get_input_line_start());
    for (pos = tty->line_start; pos != tty->write_pos; pos = (pos + 1) % TTY_BUFFER_SIZE)
        putc(tty->buffer[pos]);

    if (old_len > (uint32_t)i)
    {
        blanks = old_len - (uint32_t)i;
        while (blanks--)
            putc(' ');
        set_cursor_position(get_input_line_start() + (int)i);
    }
}

size_t tty_current_line(tty_device_t *tty, char *out, size_t out_size)
{
    uint32_t pos = tty->line_start;
    size_t n = 0;

    while (pos != tty->write_pos && n < out_size)
    {
        out[n++] = tty->buffer[pos];
        pos = (pos + 1) % TTY_BUFFER_SIZE;
    }
    return n;
}

int tty_keyboard_input(void *fp, const char *buf, size_t count)
{
    tty_device_t *tty = (tty_device_t *)fp;
    size_t i;

    for (i = 0; i < count; i++)
    {
        tty->buffer[tty->write_pos] = buf[i];
        tty->write_pos = (tty->write_pos + 1) % TTY_BUFFER_SIZE;
        putc(buf[i]);
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
