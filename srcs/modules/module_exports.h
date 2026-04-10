#ifndef MODULE_EXPORTS_H
#define MODULE_EXPORTS_H

typedef struct
{
    const char *name;
    void       *addr;
} kernel_symbol_t;

#define EXPORT_SYMBOL(sym) \
    static kernel_symbol_t __ksym_##sym \
    __attribute__((__section__("__ksymtab"))) \
    __attribute__((__used__)) = { #sym, (void*)&sym }

#endif /* MODULE_EXPORTS_H */
