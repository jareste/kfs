#include "memory.h"
#include "../utils/stdint.h"
#include "../utils/utils.h"
void* kstrdup(const char *s)
{
    size_t len = strlen(s) + 1;
    char *p = kmalloc(len);
    if (p) memcpy(p, s, len);
    return p;
}
