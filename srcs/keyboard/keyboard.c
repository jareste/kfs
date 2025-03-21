#include "../utils/stdint.h"
#include "../display/display.h"
#include "../io/io.h"
#include "../time/time.h"
#include "idt.h"
#include "../memory/memory.h"
#include "../tasks/task.h"
#include "keyboard.h"

#define KEYBOARD_DATA_PORT 0x60

#define KEYBOARD_DATA_PORT 0x60
#define KEYBOARD_STATUS_PORT 0x64

#define KEYBOARD_BUFFER_SIZE 256
static char keyboard_buffer[KEYBOARD_BUFFER_SIZE] = {0};
static int keyb_buff_start = 0;
static int keyb_buff_end = 0;

bool shift_pressed = false;
bool ctrl_pressed = false;

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
    while ((c = get_last_char()) == '\0') scheduler();
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

    delete_last_char();
}

void clear_kb_buffer()
{
    keyb_buff_start = 0;
    keyb_buff_end = 0;
    memset(keyboard_buffer, 0, KEYBOARD_BUFFER_SIZE);
}

int write_stdin_wrapper(int fd, const char *buf, size_t count)
{
    int i;

    for (i = 0; i < count; i++)
    {
        set_kb_char(buf[i]);
    }
    return count;
}

int read_stdin_wrapper(int fd, char *buf, size_t count)
{
    int i;

    for (i = 0; i < count; i++)
    {
        buf[i] = get_last_char_blocking();
    }
    return count;
}

void broadcast_to_tty(char key)
{
    task_t* task = get_current_task();

    while (task)
    {
        if (task->is_user == true)
        {
            if (task->fd_table[0] == true && task->fd_pointers[0].type == FD_TTY)
            {
                task->fd_pointers[0].fops.write(task->fd_pointers[0].fp, &key, 1);
            }
        }
        task = task->next;
        if (task == get_current_task())
            break;
    }
}

void keyboard_handler()
{
    uint8_t scancode = inb(KEYBOARD_DATA_PORT);
    char key = 0;
    bool key_released = scancode & 0x80;

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
            if (ctrl_pressed)
            {
                delete_until_char();
            }
            else
            {
                delete_last_kb_char();
                broadcast_to_tty('\b');
            }
            break;
        default:
            key = get_ascii_char(scancode, shift_pressed);
            if (key)
            {
                // tty_write_ch(key);
                // get_current_task()->fd_pointers[1].fops.write(1, &key, 1);
                /* echo must not be here as we don't know which task is into CPU when
                 * interrupt is triggered
                 */
                // if (get_current_task()->screen_echo == true)
                //     putc(key);
                broadcast_to_tty(key);
                set_kb_char(key);
            }
    }

    if (key_released)
        dispatch_key_event(scancode &= 0x7F, key_released);
    else
        dispatch_key_event(scancode, key_released);
    outb(PIC1_COMMAND, PIC_EOI);
}
