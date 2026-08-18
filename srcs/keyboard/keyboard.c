#include "../utils/stdint.h"
#include "../display/display.h"
#include "../io/io.h"
#include "../time/time.h"
#include "idt.h"
#include "../memory/memory.h"
#include "../tasks/task.h"
#include "../display/tty/tty.h"
#include "keyboard.h"
#include "signals.h"

#define KEYBOARD_DATA_PORT 0x60

#define KEYBOARD_DATA_PORT 0x60
#define KEYBOARD_STATUS_PORT 0x64

#define KEYBOARD_BUFFER_SIZE 256
#define CMD_HISTORY_SIZE      32
#define CMD_HISTORY_LINE_MAX  256

static char keyboard_buffer[KEYBOARD_BUFFER_SIZE] = {0};
static int keyb_buff_start = 0;
static int keyb_buff_end = 0;

static bool shift_pressed = false;
static bool ctrl_pressed = false;
static bool at_line_start = true;

static volatile bool kb_eof_flag = false;

static char   cmd_history[CMD_HISTORY_SIZE][CMD_HISTORY_LINE_MAX];
static size_t cmd_history_len[CMD_HISTORY_SIZE];
static int    cmd_history_count = 0;  /* valid entries, <= CMD_HISTORY_SIZE */
static int    cmd_history_head = 0;   /* next slot to write */
static int    cmd_history_browse = -1; /* -1 = not browsing; else steps back from the newest entry */

static void arm_line_start_if_needed(void)
{
    if (at_line_start)
    {
        task_t *fg = get_task_by_pid(get_foreground_pid());

        mark_input_line_start();
        if (fg && fg->fd_table[0] && fg->fd_pointers[0].type == FD_TTY)
            tty_mark_line_start((tty_device_t *)fg->fd_pointers[0].fp);
        at_line_start = false;
    }
}

static void history_push(const char *line, size_t len)
{
    if (len == 0)
        return; /* don't clutter history with bare Enters */
    if (len >= CMD_HISTORY_LINE_MAX)
        len = CMD_HISTORY_LINE_MAX - 1;

    memcpy(cmd_history[cmd_history_head], line, len);
    cmd_history_len[cmd_history_head] = len;
    cmd_history_head = (cmd_history_head + 1) % CMD_HISTORY_SIZE;
    if (cmd_history_count < CMD_HISTORY_SIZE)
        cmd_history_count++;
}

static void history_browse_reset(void)
{
    cmd_history_browse = -1;
}

/* direction < 0: older (Up). direction > 0: newer (Down). */
static void history_recall(tty_device_t *tty, int direction)
{
    int next, idx;

    if (direction < 0)
    {
        if (cmd_history_count == 0)
            return;
        next = (cmd_history_browse < 0) ? 0 : cmd_history_browse + 1;
        if (next >= cmd_history_count)
            return; /* already at the oldest entry */
    }
    else
    {
        if (cmd_history_browse < 0)
            return; /* not browsing -- nothing "newer" to go to */
        next = cmd_history_browse - 1;
    }

    cmd_history_browse = next;

    if (cmd_history_browse < 0)
    {
        tty_recall_line(tty, "", 0);
        return;
    }

    idx = (cmd_history_head - 1 - cmd_history_browse + CMD_HISTORY_SIZE) % CMD_HISTORY_SIZE;
    tty_recall_line(tty, cmd_history[idx], cmd_history_len[idx]);
}

void kb_raise_eof(void)
{
    kb_eof_flag = true;
}

bool kb_eof_pending(void)
{
    return kb_eof_flag;
}

void kb_clear_eof(void)
{
    kb_eof_flag = false;
}

char get_last_char()
{
    if (keyb_buff_start == keyb_buff_end)
    {
        return '\0';
    }

    char c = keyboard_buffer[keyb_buff_start];
    keyb_buff_start = (keyb_buff_start + 1) % KEYBOARD_BUFFER_SIZE;
    return c;
}

char get_last_char_blocking()
{
    char c;
    while ((c = get_last_char()) == '\0')
    {
        asm volatile("hlt;");
    }
    return c;
}

char* get_kb_buffer()
{
    return keyboard_buffer;
}

static void set_kb_char(char c)
{
    keyboard_buffer[keyb_buff_end] = c;
    
    keyb_buff_end = (keyb_buff_end + 1) % KEYBOARD_BUFFER_SIZE;
}

static void delete_last_kb_char()
{
    if (keyb_buff_end == 0)
    {
        return;
    }
    else
    {
        keyb_buff_end--;
    }
    keyboard_buffer[keyb_buff_end] = 0;

    if (keyb_buff_start > keyb_buff_end)
    {
        keyb_buff_start = keyb_buff_end;
    }
}

void clear_kb_buffer()
{
    keyb_buff_start = 0;
    keyb_buff_end = 0;
    memset(keyboard_buffer, 0, KEYBOARD_BUFFER_SIZE);
}

int write_stdin_wrapper(int fd, const char *buf, size_t count)
{
    size_t i;

    (void)fd;
    for (i = 0; i < count; i++)
    {
        set_kb_char(buf[i]);
    }
    return count;
}

int read_stdin_wrapper(int fd, char *buf, size_t count)
{
    size_t i;

    (void)fd;
    for (i = 0; i < count; i++)
    {
        buf[i] = get_last_char_blocking();
    }
    return count;
}

static bool broadcast_to_tty(char key)
{
    task_t *task = get_task_by_pid(get_foreground_pid());

    if (task && task->is_user && task->fd_table[0] &&
        task->fd_pointers[0].type == FD_TTY)
    {
        tty_insert_char((tty_device_t *)task->fd_pointers[0].fp, key);
        return true;
    }
    return false;
}

void keyboard_handler()
{
    uint8_t scancode = inb(KEYBOARD_DATA_PORT);
    char key = 0;
    bool key_released = scancode & 0x80;
    task_t* fg_task;
    tty_device_t* tty;
    pid_t pid;

    switch (scancode)
    {
        case 0x2A:
        case 0x36:
            shift_pressed = true;
            break;
        case 0xAA:
        case 0xB6:
            shift_pressed = false;
            break;
        case 0x1D:
            ctrl_pressed = true;
            break;
        case 0x9D:
            ctrl_pressed = false;
            break;
        case 0x4B:
            arm_line_start_if_needed();
            fg_task = get_task_by_pid(get_foreground_pid());
            if (fg_task && fg_task->fd_table[0] && fg_task->fd_pointers[0].type == FD_TTY)
                tty_move_edit_cursor((tty_device_t *)fg_task->fd_pointers[0].fp, -1);
            break;
        case 0x4D:
            arm_line_start_if_needed();
            fg_task = get_task_by_pid(get_foreground_pid());
            if (fg_task && fg_task->fd_table[0] && fg_task->fd_pointers[0].type == FD_TTY)
                tty_move_edit_cursor((tty_device_t *)fg_task->fd_pointers[0].fp, 1);
            break;
        case 0x48:
            arm_line_start_if_needed();
            fg_task = get_task_by_pid(get_foreground_pid());
            if (fg_task && fg_task->fd_table[0] && fg_task->fd_pointers[0].type == FD_TTY)
                history_recall((tty_device_t *)fg_task->fd_pointers[0].fp, -1);
            break;
        case 0x50:
            arm_line_start_if_needed();
            fg_task = get_task_by_pid(get_foreground_pid());
            if (fg_task && fg_task->fd_table[0] && fg_task->fd_pointers[0].type == FD_TTY)
                history_recall((tty_device_t *)fg_task->fd_pointers[0].fp, 1);
            break;
        case 0x49:
            scroll_view_up(SCREEN_HEIGHT);
            break;
        case 0x51:
            scroll_view_down(SCREEN_HEIGHT);
            break;
        case 0x47: /* Home */
            arm_line_start_if_needed();
            fg_task = get_task_by_pid(get_foreground_pid());
            if (fg_task && fg_task->fd_table[0] && fg_task->fd_pointers[0].type == FD_TTY)
                tty_move_edit_cursor((tty_device_t *)fg_task->fd_pointers[0].fp, -TTY_BUFFER_SIZE);
            break;
        case 0x4F: /* End */
            arm_line_start_if_needed();
            fg_task = get_task_by_pid(get_foreground_pid());
            if (fg_task && fg_task->fd_table[0] && fg_task->fd_pointers[0].type == FD_TTY)
                tty_move_edit_cursor((tty_device_t *)fg_task->fd_pointers[0].fp, TTY_BUFFER_SIZE);
            break;
        case 0x0F:
            puts("    ");
            break;
        case 0x53:
            arm_line_start_if_needed();
            fg_task = get_task_by_pid(get_foreground_pid());
            if (fg_task && fg_task->fd_table[0] && fg_task->fd_pointers[0].type == FD_TTY)
                tty_delete_forward((tty_device_t *)fg_task->fd_pointers[0].fp);
            break;
        case 0x01:
            clear_screen();
            break;
        case 0x0E:
            arm_line_start_if_needed();
            if (ctrl_pressed)
            {
                delete_until_char();
            }
            else
            {
                delete_last_kb_char();
                fg_task = get_task_by_pid(get_foreground_pid());
                if (fg_task && fg_task->fd_table[0] && fg_task->fd_pointers[0].type == FD_TTY)
                    tty_backspace_at_cursor((tty_device_t *)fg_task->fd_pointers[0].fp);
                else
                    delete_last_char(); /* no foreground tty (e.g. kshell): erase directly */
            }
            break;
        default:
            key = get_ascii_char(scancode, shift_pressed);

            /* Ctrl+<key> combos: these never reach the buffer as ordinary
             * characters -- they either raise a signal on the foreground
             * task (SIGINT/SIGQUIT) or an EOF condition (Ctrl+D), exactly
             * like a real tty line discipline with ISIG set.
             */
            if (ctrl_pressed && key && !key_released)
            {
                pid = get_foreground_pid();

                if (key == 'c')
                {
                    if (pid >= 0)
                        _kill(pid, SIGINT);
                    puts("^C\n");
                    at_line_start = true;
                    break;
                }
                if (key == '\\')
                {
                    if (pid >= 0)
                        _kill(pid, SIGQUIT);
                    puts("^\\\n");
                    at_line_start = true;
                    break;
                }
                if (key == 'd')
                {
                    /* Raw keyboard-buffer readers (get_line(), used by
                     * kshell) pick this up directly.
                     */
                    kb_raise_eof();

                    /* tty_read() readers (user tasks, e.g. a shell run via
                     * execve) get an EOF marker pushed into their own tty
                     * buffer, so a blocked read() returns 0.
                     */
                    fg_task = get_task_by_pid(pid);
                    if (fg_task && fg_task->fd_table[0] &&
                        fg_task->fd_pointers[0].type == FD_TTY)
                    {
                        tty = (tty_device_t *)fg_task->fd_pointers[0].fp;
                        tty->buffer[tty->write_pos] = 0x04; /* ASCII EOT */
                        tty->write_pos = (tty->write_pos + 1) % TTY_BUFFER_SIZE;
                    }
                    puts("^D\n");
                    at_line_start = true;
                    break;
                }
            }

            if (key)
            {
                arm_line_start_if_needed();
                if (key == '\n')
                {
                    /* Capture the line into history *before* the newline
                     * that submits it lands in the buffer. */
                    fg_task = get_task_by_pid(get_foreground_pid());
                    if (fg_task && fg_task->fd_table[0] && fg_task->fd_pointers[0].type == FD_TTY)
                    {
                        char linebuf[CMD_HISTORY_LINE_MAX];
                        tty = (tty_device_t *)fg_task->fd_pointers[0].fp;
                        size_t linelen = tty_current_line(tty, linebuf, sizeof(linebuf));
                        history_push(linebuf, linelen);
                        tty_write_ch(tty, key); /* always append at the true end, not the edit cursor */
                    }
                    else
                    {
                        broadcast_to_tty(key);
                    }
                    history_browse_reset();
                }
                else
                {
                    broadcast_to_tty(key);
                }
                set_kb_char(key);
                if (key == '\n')
                    at_line_start = true;
            }
    }

    if (key_released)
        dispatch_key_event(scancode &= 0x7F, key_released);
    else
        dispatch_key_event(scancode, key_released);
    outb(PIC1_COMMAND, PIC_EOI);
}
