#ifndef MODULES_H
#define MODULES_H

#include "../utils/stdint.h"
#include "../utils/utils.h"
#include "../time/time.h"

typedef int  (*module_init_fn_t)(void);
typedef void (*module_exit_fn_t)(void);

#define MODULE_INIT(fn) \
    module_init_fn_t __mod_init_func \
    __attribute__((__section__(".mod_init"))) \
    __attribute__((__used__)) = fn

#define MODULE_EXIT(fn) \
    module_exit_fn_t __mod_exit_func \
    __attribute__((__section__(".mod_exit"))) \
    __attribute__((__used__)) = fn

#define MODULE_NAME(n) \
    const char *__mod_name \
    __attribute__((__section__(".mod_info"))) \
    __attribute__((__used__)) = n

#define MODULE_FLAGS(f) \
    module_flags_t __mod_flags \
    __attribute__((__section__(".mod_info_flags"))) \
    __attribute__((__used__)) = f

/* https://man7.org/linux/man-pages/man2/init_module.2.html */
struct module
{
    unsigned long         size_of_struct;
    struct module        *next;
    const char           *name;
    unsigned long         size;
    long                  usecount;
    unsigned long         flags;
    unsigned int          nsyms;
    unsigned int          ndeps;
    struct module_symbol *syms;
    struct module_ref    *deps;
    struct module_ref    *refs;
    typeof(int (void))   *init;
    typeof(void (void))  *cleanup;
    const struct exception_table_entry *ex_table_start;
    const struct exception_table_entry *ex_table_end;
#ifdef __alpha__
    unsigned long gp;
#endif
};

/* Flags to indicate which events a module cares about */
typedef enum
{
    MODULE_FLAG_NONE       = 0x0,
    MODULE_FLAG_KEYBOARD   = 0x0001,
    MODULE_FLAG_CPU_CYCLE  = 0x0002,
    MODULE_FLAG_TIME       = 0x0004,
} module_flags_t;

struct kernel_services;

/* Module interface structure */
typedef struct
{
    int module_id; /* must be overwritten by the kernel whatever the user introduced here */
    const char *name;
    module_flags_t flags;
    
    /* Lifecycle functions */
    int  (*init)();
    void (*cleanup)();
    void (*read)(char *buffer, size_t size, size_t* offset);

    /* Event callbacks */
    void (*on_key_event)(int key, int state);
    void (*on_cpu_cycle)();
    void (*on_time_request)(timespec_t *timeData);
} module_t;

/* API to register and unregister modules */
int register_module(module_t *module);
int unregister_module(module_t *module);

/* Dispatch functions used by the kernel to notify events */
void dispatch_key_event(int key, int state);
void dispatch_cpu_cycle(void);
void dispatch_time_request(timespec_t *timeData);

module_t *get_module_by_id(int module_id);

void *module_alloc(size_t size);
void  module_free(void *ptr);

#endif /* MODULES_H */
