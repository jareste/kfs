#include "keyboard.h"
#include "../utils/utils.h"
#include "../tasks/task.h"

char* get_line()
{
    char c;
    clear_kb_buffer();
    while ((c = getc()) != '\n')
    {
        if (get_current_task()->screen_echo == true && c != '\0')
        {
            putc(c);
        }
        scheduler();
    }
    if (get_current_task()->screen_echo == true && c != '\0')
    {
        putc(c);
    }
    char* buffer = get_kb_buffer();
    buffer[strlen(buffer) - 1] = '\0'; /* remove '\n' */
    return buffer;
}
