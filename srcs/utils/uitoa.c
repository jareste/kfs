#include "stdint.h"
void uitoa(unsigned int value, char *str, int base)
{
    char *rc;
    char *ptr;
    char *low;

    rc = ptr = str;

    *ptr++ = '\0';

    do
    {
        *ptr++ = "0123456789abcdef"[value % base];
        value /= base;
    } while (value);

    low = rc;
    --ptr;
    while (low < ptr)
    {
        char tmp = *low;
        *low++ = *ptr;
        *ptr-- = tmp;
    }
}
