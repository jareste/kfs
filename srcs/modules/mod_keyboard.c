#include "modules.h"
#include "../display/display.h"

/* Module initialization */
static int keyboard_init(module_t *self, struct kernel_services *services)
{
    printf("[Keyboard Module] Initialized.\n");
    return 0;
}

/* Module cleanup */
static void keyboard_cleanup(module_t *self)
{
    printf("[Keyboard Module] Cleanup.\n");
}

const char* keyboard_buffer = "Hello, World!\n que tal?\n jajaja\n";

static keyboard_read(module_t *self, char *buffer, size_t size, size_t* offset)
{
    size_t len = strlen(keyboard_buffer);
    if (*offset >= len)
    {
        *offset = 0;
        return;
    }
    size_t remaining = len - *offset;
    size_t to_copy = (remaining < size) ? remaining : size;
    memcpy(buffer, keyboard_buffer + *offset, to_copy);
    *offset += to_copy;
}

/* Callback for key events */
static void keyboard_on_key_event(module_t *self, int key, int state)
{
    // printf("[Keyboard Module] Key %d %s.\n", key, (state) ? "pressed" : "released");
}

/* Define the module instance */
module_t keyboard_module = {
    .module_id = 1,
    .name = "keyboard_module",
    .flags = MODULE_FLAG_KEYBOARD,
    .init = keyboard_init,
    .cleanup = keyboard_cleanup,
    .read = keyboard_read,
    .on_key_event = keyboard_on_key_event,
    .on_cpu_cycle = NULL,
    .on_time_request = NULL,
    .private_data = NULL
};

void register_keyboard_module()
{
    register_module(&keyboard_module);
}

void unregister_keyboard_module()
{
    unregister_module(&keyboard_module);
}
