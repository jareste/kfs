#include "display.h"

static int cursor_position = 0;
static int color = LIGHT_GREY;
static bool ofuscated = false;
static bool can_print = true;

static int line_start = 0;

void mark_input_line_start(void)
{
    line_start = cursor_position;
}

void enable_print()
{
    can_print = true;
}

void disable_print()
{
    can_print = false;
}

void start_ofuscation()
{
    ofuscated = true;
}

void stop_ofuscation()
{
    ofuscated = false;
}

void set_putchar_color(uint8_t c)
{
    color = c;
}

static void scroll_screen()
{
    char *video_memory = (char *)VIDEO_MEMORY;

    for (int i = 0; i < (SCREEN_HEIGHT - 1) * SCREEN_WIDTH * 2; i++)
    {
        video_memory[i] = video_memory[i + SCREEN_WIDTH * 2];
    }

    for (int i = (SCREEN_HEIGHT - 1) * SCREEN_WIDTH * 2;\
            i < SCREEN_HEIGHT * SCREEN_WIDTH * 2; i += 2)
    {
        video_memory[i] = '\0';
        video_memory[i + 1] = LIGHT_GREY;
    }
}

void update_cursor(int position)
{
    outb(0x3D4, 14); /* Send the high byte of the cursor position to the VGA controller */
    outb(0x3D5, (position >> 8) & 0xFF);
    outb(0x3D4, 15); /* Send the low byte of the cursor position to the VGA controller */
    outb(0x3D5, position & 0xFF);
}

void clear_screen()
{
    char *video_memory = (char *)VIDEO_MEMORY;
    for (int y = 0; y < SCREEN_HEIGHT; y++)
    {
        for (int x = 0; x < SCREEN_WIDTH; x++)
        {
            int offset = (y * SCREEN_WIDTH + x) * 2;
            video_memory[offset] = '\0';
            video_memory[offset + 1] = 0x07;
        }
    }
    cursor_position = 0;
    line_start = 0;
    update_cursor(cursor_position);
}

void delete_last_char()
{
    char *video_memory = (char *)VIDEO_MEMORY;
    if (cursor_position > line_start)
    {
        cursor_position--;
        video_memory[cursor_position * 2] = '\0';
        video_memory[cursor_position * 2 + 1] = LIGHT_GREY;
        update_cursor(cursor_position);
    }
}

void delete_actual_char()
{
    char *video_memory = (char *)VIDEO_MEMORY;
    if (cursor_position >= 0)
    {
        video_memory[cursor_position * 2] = ' ';
        video_memory[cursor_position * 2 + 1] = LIGHT_GREY;
    }
}

void delete_until_char()
{
    char *video_memory = (char *)VIDEO_MEMORY;

    if (cursor_position > line_start)
    {
        cursor_position--;

        while (cursor_position >= line_start && video_memory[cursor_position * 2] == ' ')
        {
            video_memory[cursor_position * 2] = ' ';
            video_memory[cursor_position * 2 + 1] = LIGHT_GREY;
            cursor_position--;
        }

        if (cursor_position >= line_start)
        {
            cursor_position++;
        }
        else
        {
            cursor_position = line_start;
        }

        update_cursor(cursor_position);
    }
}

void move_cursor_right()
{
    cursor_position++;
    if (cursor_position >= SCREEN_WIDTH * SCREEN_HEIGHT)
    {
        scroll_screen();
        cursor_position -= SCREEN_WIDTH;
    }
    update_cursor(cursor_position);
}

void move_cursor_left()
{
    if (cursor_position <= 0)
    {
        return;
    }
    cursor_position--;
    update_cursor(cursor_position);
}

void move_cursor_up()
{
    if (cursor_position >= SCREEN_WIDTH)
    {
        cursor_position -= SCREEN_WIDTH;
        update_cursor(cursor_position);
    }
}

void move_cursor_down()
{
    if (cursor_position < MAX_CURSOR_POSITION - SCREEN_WIDTH)
    {
        cursor_position += SCREEN_WIDTH;
        update_cursor(cursor_position);
    }
}

void putc_color(char c, uint8_t color)
{
    char* video_memory;

    if (!can_print)
    {
        return;
    }

    video_memory = (char *)VIDEO_MEMORY;

    if (ofuscated)
    {
        c = c == '\n' ? c : '*';
    }

    if (c == '\n')
    {
        cursor_position += SCREEN_WIDTH - (cursor_position % SCREEN_WIDTH);
        update_cursor(cursor_position);
    }
    else if (c == '\r')
    {
        cursor_position -= (cursor_position % SCREEN_WIDTH);
        update_cursor(cursor_position);
    }
    else if (c == '\b')
    {
        if (cursor_position > line_start)
        {
            cursor_position--;
            video_memory[cursor_position * 2] = '\0';
            video_memory[cursor_position * 2 + 1] = LIGHT_GREY;
            update_cursor(cursor_position);
        }
    }
    else
    {
        video_memory[cursor_position * 2] = c;
        video_memory[cursor_position * 2 + 1] = color;
        cursor_position++;
        update_cursor(cursor_position);
    }

    if (cursor_position >= SCREEN_WIDTH * SCREEN_HEIGHT)
    {
        scroll_screen();
        cursor_position -= SCREEN_WIDTH;
        line_start -= SCREEN_WIDTH;
        if (line_start < 0)
            line_start = 0;
        update_cursor(cursor_position);
    }
}

void putc(char c)
{
    putc_color(c, color);
}
