#include "utils.h"
char *strrchr(const char *s, int c)
{
    const char *last = NULL;
    while (*s)
    {
        if (*s == c)
        {
            last = s;
        }
        s++;
    }
    return (char *)last;
}
