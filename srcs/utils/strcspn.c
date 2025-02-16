#include "stdint.h"
size_t strcspn(const char *s, const char *reject)
{
    size_t i = 0;
    while (s[i])
    {
        size_t j = 0;
        while (reject[j])
        {
            if (s[i] == reject[j])
            {
                return i;
            }
            j++;
        }
        i++;
    }
    return i;
}
