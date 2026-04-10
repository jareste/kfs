#include "modules.h"
#include "module_exports.h"
#include "../ide/ext2_fileio.h"
#include "../display/display.h"
#include "../memory/memory.h"

typedef struct module_node
{
    module_t* module;
    struct module_node* next;
} module_node_t;

static module_node_t* module_list = NULL;
static int module_count = 0;
static int next_module_id = 1;

static module_node_t *find_node(module_t *module)
{
    module_node_t *n;

    for (n = module_list; n; n = n->next)
        if (n->module == module)
            return n;
    return NULL;
}

static module_node_t *find_node_by_id(int module_id)
{
    module_node_t *n;

    for (n = module_list; n; n = n->next)
        if (n->module->module_id == module_id)
            return n;
    return NULL;
}

static module_node_t *find_node_by_name(const char *name)
{
    module_node_t *n;

    for (n = module_list; n; n = n->next)
        if (strcmp(n->module->name, name) == 0)
            return n;
    return NULL;
}

/* Public API */
int register_module(module_t *module)
{
    module_node_t* node;

    if (!module || !module->name)
        return -1;

    if (find_node_by_name(module->name))
    {
        kprintf("register_module: '%s' already registered\n", module->name);
        return -1;
    }

    if (create_device_node("/dev", module->name, module) < 0)
    {
        kprintf("register_module: could not create /dev/%s\n", module->name);
        return -1;
    }

    node = kmalloc(sizeof(module_node_t));
    if (!node)
    {
        kprintf("register_module: out of memory\n");
        delete_device_node("/dev", module->name);
        return -1;
    }

    module->module_id = next_module_id++;
    node->module      = module;
    node->next        = module_list;
    module_list       = node;
    module_count++;

    return 0;
}

int unregister_module(module_t *module)
{
    module_node_t* dead;
    module_node_t** cur;
    if (!module)
        return -1;

    /* TODO if it would have any reference, i must check for it before deleting it. */
    cur = &module_list;
    while (*cur)
    {
        if ((*cur)->module == module)
        {
            dead = *cur;
            *cur = dead->next;
            module_count--;

            if (module->cleanup)
                module->cleanup(module);

            delete_device_node("/dev", module->name);
            kfree(dead);
            return 0;
        }
        cur = &(*cur)->next;
    }

    kprintf("unregister_module: '%s' not found\n", module->name);
    return -1;
}

module_t *get_module_by_id(int module_id)
{
    module_node_t *n = find_node_by_id(module_id);
    return n ? n->module : NULL;
}

module_t *get_module_by_name(const char *name)
{
    module_node_t *n = find_node_by_name(name);
    return n ? n->module : NULL;
}

int get_module_count(void)
{
    return module_count;
}

/* Dispatchers */
void dispatch_key_event(int key, int state)
{
    module_node_t *n;
    module_t *m;

    for (n = module_list; n; n = n->next)
    {
        m = n->module;
        if ((m->flags & MODULE_FLAG_KEYBOARD) && m->on_key_event)
            m->on_key_event(key, state);
    }
}

void dispatch_cpu_cycle(void)
{
    module_node_t *n;
    module_t *m;

    for (n = module_list; n; n = n->next)
    {
        m = n->module;
        if ((m->flags & MODULE_FLAG_CPU_CYCLE) && m->on_cpu_cycle)
            m->on_cpu_cycle(m);
    }
}

void dispatch_time_request(struct time_info *timeData)
{
    module_node_t *n;
    module_t *m;

    for (n = module_list; n; n = n->next)
    {
        m = n->module;
        if ((m->flags & MODULE_FLAG_TIME) && m->on_time_request)
            m->on_time_request(timeData);
    }
}

void dispatch_read_request(int module_id, char *buffer, size_t size, size_t *offset)
{
    module_t *m = get_module_by_id(module_id);
    if (m && m->read)
        m->read(buffer, size, offset);
}

/* -------------------------------
    Simple module memory allocator
   -------------------------------
*/
#define MODULE_MEM_RING_SIZE (1024 * 1024)
static uint8_t module_mem_ring[MODULE_MEM_RING_SIZE];
static size_t mem_ring_offset = 0;

void *module_alloc(size_t size)
{
    if (mem_ring_offset + size > MODULE_MEM_RING_SIZE)
    {
        kprintf("Error: Module memory ring exhausted.\n");
        return NULL;
    }
    void *ptr = &module_mem_ring[mem_ring_offset];
    mem_ring_offset += size;
    return ptr;
}

void module_free(void *ptr) 
{
    /* In a simple ring allocator, free is not implemented.
       A real implementation would require a more sophisticated scheme. */
}
