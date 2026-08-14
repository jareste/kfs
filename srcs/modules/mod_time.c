#include "modules.h"
#include "mod_time.h"
#include "../display/display.h"
#include "../time/time.h"
#include "../utils/stdint.h"
#include "../syscalls/syscalls.h"

/* Module initialization */
static int time_init(module_t *self, struct kernel_services *services)
{
    (void)self;
    (void)services;
    kprintf("[Time Module] Initialized.\n");
    return 0;
}

/* Module cleanup */
static void time_cleanup(module_t *self)
{
    (void)self;
    kprintf("[Time Module] Cleanup.\n");
}

/* Callback for time requests */
static void time_on_request(timespec_t *timeData)
{
    timeData->tv_sec = sys_time(NULL); /* no need to go through int, as we are in kernel. */
}

/* Define the module instance */
module_t time_module = {
    .module_id = 2,
    .name = "mod_time",
    .flags = MODULE_FLAG_TIME,
    .init = time_init,
    .cleanup = time_cleanup,
    .read = NULL,
    .on_key_event = NULL,
    .on_cpu_cycle = NULL,
    .on_time_request = time_on_request,
};

void register_time_module()
{
    register_module(&time_module);
}
