#include "memory.h"
#include "../utils/stdint.h"
#include "../utils/utils.h"
void* vstrdup(const char *s)
{
    size_t len = strlen(s) + 1;
    char *p = vmalloc(len, true);
    if (p) memcpy(p, s, len);
    return p;
}
