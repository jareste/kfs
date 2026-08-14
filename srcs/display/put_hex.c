#include "display.h"

int put_hex(uint32_t n)
{
    char hex[16] = "0123456789ABCDEF";
    char buffer[9];

    for (int j = 0; j < 8; j++)
    {
        buffer[j] = hex[(n >> (28 - j * 4)) & 0xF];
    }
    buffer[8] = '\0';

    puts(buffer);

    return 0;
}

int put_2_hex(uint8_t n)
{
    char hex[16] = "0123456789ABCDEF";
    char buffer[3];

    for (int j = 0; j < 2; j++)
    {
        buffer[j] = hex[(n >> (4 - j * 4)) & 0xF];
    }
    buffer[2] = '\0';

    puts(buffer);

    return 0;
}
