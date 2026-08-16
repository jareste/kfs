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
static char keyboard_buffer[KEYBOARD_BUFFER_SIZE] = {0};
static int keyb_buff_start = 0;
static int keyb_buff_end = 0;

static bool shift_pressed = false;
static bool ctrl_pressed = false;
static bool at_line_start = true;

static void arm_line_start_if_needed(void)
{
    if (at_line_start)
    {
        mark_input_line_start();
        at_line_start = false;
    }
}

static volatile bool kb_eof_flag = false;

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
        task->fd_pointers[0].type == FD_TTY) {
        tty_keyboard_input(task->fd_pointers[0].fp, &key, 1);
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
            // move_cursor_left();
            break;
        case 0x4D:
            // move_cursor_right();
            break;
        case 0x48:
            // move_cursor_up();
            break;
        case 0x50:
            // move_cursor_down();
            break;
        case 0x0F:
            puts("    ");
            break;
        case 0x53:
            // delete_actual_char(); // better do nothing for now. it's delete
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
                if (!broadcast_to_tty('\b'))
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
                broadcast_to_tty(key);
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
