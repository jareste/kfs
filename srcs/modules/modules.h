#ifndef MODULES_H
#define MODULES_H

#include "../utils/stdint.h"
#include "../utils/utils.h"

/* Flags to indicate which events a module cares about */
typedef enum
{
    MODULE_FLAG_NONE       = 0x0,
    MODULE_FLAG_KEYBOARD   = 0x0001,
    MODULE_FLAG_CPU_CYCLE  = 0x0002,
    MODULE_FLAG_TIME       = 0x0004,
} module_flags_t;

/* Forward declarations in case you want to extend kernel services or time info */
struct kernel_services;
struct time_info;

/* Module interface structure */
typedef struct module
{
    int module_id; /* must be overwritten by the kernel whatever the user introduced here */
    const char *name;
    module_flags_t flags;
    
    /* Lifecycle functions */
    int  (*init)(struct module *self, struct kernel_services *services);
    void (*cleanup)(struct module *self);
    void (*read)(struct module *self, char *buffer, size_t size, size_t* offset);

    /* Event callbacks */
    void (*on_key_event)(struct module *self, int key, int state);
    void (*on_cpu_cycle)(struct module *self);
    void (*on_time_request)(struct module *self, struct time_info *timeData);
    
    /* Private module-specific data pointer */
    void *private_data;
} module_t;

/* API to register and unregister modules */
int register_module(module_t *module);
int unregister_module(module_t *module);

/* Dispatch functions used by the kernel to notify events */
void dispatch_key_event(int key, int state);
void dispatch_cpu_cycle(void);
void dispatch_time_request(struct time_info *timeData);

module_t *get_module_by_id(int module_id);

/* Example of special memory allocation functions for modules 
   using a memory ring dedicated to modules. */
void *module_alloc(size_t size);
void  module_free(void *ptr); // In our simple ring, free is a no-op

#endif // MODULES_H
