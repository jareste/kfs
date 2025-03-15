#include "display.h"

int write_stdout_wrapper(int fd, const char *buf, size_t count)
{
    int i;

    for (i = 0; i < count; i++)
    {
        putc(buf[i]);
    }
    return count;
}

int write_stderr_wrapper(int fd, const char *buf, size_t count)
{
    int i;

    for (i = 0; i < count; i++)
    {
        putc(buf[i]);
    }
    return count;
}

int puts(const char *str)
{
    int i = 0;
    while (str[i])
    {
        putc(str[i]);
        i++;
    }
    return i;
}

int puts_color(const char *str, uint8_t color)
{
    int i = 0;
    while (str[i])
    {
        putc_color(str[i], color);
        i++;
    }
    return i;
}
