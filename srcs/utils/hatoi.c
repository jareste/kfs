#include "stdint.h"
#include "../display/display.h"

uint32_t hex_string_to_int(const char *hex_str)
{
    uint32_t result = 0;

    for (int i = 0; hex_str[i] != '\0'; i++)
    {
        result <<= 4;

        if (hex_str[i] >= '0' && hex_str[i] <= '9')
        {
            result += hex_str[i] - '0';
        }
        else if (hex_str[i] >= 'a' && hex_str[i] <= 'f')
        {
            result += hex_str[i] - 'a' + 10;
        }
        else if (hex_str[i] >= 'A' && hex_str[i] <= 'F')
        {
            result += hex_str[i] - 'A' + 10;
        }
        else
        {
            kprintf("invalid character in hex string: %c\n", hex_str[i]);
            return 0;
        }
    }

    return result;
}
