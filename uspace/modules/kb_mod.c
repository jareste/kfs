// gcc -m32 -c -ffreestanding -fno-builtin -fno-pic -fno-pie -fno-stack-protector -nostdlib -O0  uspace/modules/kb_mod.c -o kb_mod.o

#include "../../srcs/modules/modules.h"

static void kbd_on_key(int key, int state);
static module_t kb_module = {
    .name         = "keyboard",
    .flags        = MODULE_FLAG_KEYBOARD,
    .on_key_event = kbd_on_key,
};

static void kbd_on_key(int key, int state)
{
    kprintf("key pressed: %d, state: %d\n", key, state);
}

static int kbd_init(void)
{
    kprintf("keyboard module loaded\n");
    return register_module(&kb_module);
}

static void kbd_exit(void)
{
    kprintf("keyboard module unloaded\n");
    unregister_module(&kb_module);
}

MODULE_INIT(kbd_init);
MODULE_EXIT(kbd_exit);