#include "keyboard.h"
#include "../utils/utils.h"
#include "../tasks/task.h"
#include "../display/display.h"

char* get_line()
{
    char c;

    clear_kb_buffer();
    while ((c = getc()) != '\n')
    {
        if (c == '\0')
        {
            if (kb_eof_pending() && strlen(get_kb_buffer()) == 0)
            {
                return get_kb_buffer();
            }
            continue;
        }
        if (get_current_task()->screen_echo == true && c != '\0' && c != '\b')
        {
            putc(c);
        }
    }
    if (get_current_task()->screen_echo == true && c != '\0')
    {
        putc(c);
    }
    char* buffer = get_kb_buffer();
    buffer[strlen(buffer) - 1] = '\0'; /* remove '\n' */
    return buffer;
}
