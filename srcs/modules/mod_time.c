#include "modules.h"
#include "../display/display.h"
#include "../time/time.h"
#include "../utils/stdint.h"

/* Define a structure to pass time information */
struct time_info
{
    time_t current_time;
};

/* Module initialization */
static int time_init(module_t *self, struct kernel_services *services)
{
    printf("[Time Module] Initialized.\n");
    return 0;
}

/* Module cleanup */
static void time_cleanup(module_t *self)
{
    printf("[Time Module] Cleanup.\n");
}

/* Callback for time requests */
static void time_on_request(module_t *self, struct time_info *timeData)
{
    timeData->current_time = time(NULL);
    // printf("[Time Module] Current time is %d.\n", (long)timeData->current_time);
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
    .private_data = NULL
};

void register_time_module()
{
    register_module(&time_module);
}
