#include "modules.h"
#include "../ide/ext2_fileio.h"

#define MAX_MODULES 10

static module_t* registered_modules[MAX_MODULES];
static int module_count = 0;

// int delete_device_node(const char *dir, const char *name)
// {
//     // char path[256];
//     // strcpy(path, dir);
//     // strcat(path, "/");
//     // strcat(path, name);

//     // uint32_t inode_num;
//     // if (ext2_resolve_path(path, &inode_num) < 0)
//     // {
//     //     printf("delete_device_node: file not found '%s'\n", path);
//     //     return -1;
//     // }

//     // ext2_remove_dir_entry(inode_num);
//     // ext2_free_inode(inode_num);
// }

int register_module(module_t *module)
{
    if (module_count >= MAX_MODULES)
    {
        printf("Error: Module registry full.\n");
        return -1;
    }
    registered_modules[module_count++] = module;
    module->module_id = module_count;

    if (create_device_node("/dev", module->name, module) < 0)
    {
        printf("Error: Could not create /dev/%s\n", module->name);
        return -1;
    }

    if (module->init)
    {
        return module->init(module, NULL);
    }
    return 0;
}

int unregister_module(module_t *module)
{
    int found = 0;
    for (int i = 0; i < module_count; i++)
    {
        if (registered_modules[i] == module)
        {
            if (module->cleanup)
                module->cleanup(module);

            // Remove the device node: /dev/<module->name>
            delete_device_node("/dev", module->name);

            for (int j = i; j < module_count - 1; j++)
            {
                registered_modules[j] = registered_modules[j + 1];
            }
            module_count--;
            found = 1;
            break;
        }
    }
    if (!found)
    {
        printf("Error: Module not found.\n");
        return -1;
    }
    return 0;
}

void dispatch_key_event(int key, int state)
{
    for (int i = 0; i < module_count; i++)
    {
        if (registered_modules[i]->on_key_event)
            registered_modules[i]->on_key_event(registered_modules[i], key, state);
    }
}

void dispatch_cpu_cycle(void)
{
    for (int i = 0; i < module_count; i++)
    {
        if (registered_modules[i]->on_cpu_cycle)
            registered_modules[i]->on_cpu_cycle(registered_modules[i]);
    }
}

void dispatch_time_request(struct time_info *timeData)
{
    for (int i = 0; i < module_count; i++)
    {
        if (registered_modules[i]->on_time_request)
            registered_modules[i]->on_time_request(registered_modules[i], timeData);
    }
}

void dispatch_read_request(int i, char *buffer, size_t size, size_t* offset)
{
    if (registered_modules[i]->read)
        registered_modules[i]->read(registered_modules[i], buffer, size, offset);
}

module_t *get_module_by_id(int module_id)
{
    for (int i = 0; i < module_count; i++)
    {
        if (registered_modules[i]->module_id == module_id)
            return registered_modules[i];
    }
    return NULL;
}

/* -------------------------------
   Module memory ring implementation
   -------------------------------
*/
#define MODULE_MEM_RING_SIZE (1024 * 1024) // 1 MB for modules
static uint8_t module_mem_ring[MODULE_MEM_RING_SIZE];
static size_t mem_ring_offset = 0;

void *module_alloc(size_t size)
{
    if (mem_ring_offset + size > MODULE_MEM_RING_SIZE)
    {
        printf("Error: Module memory ring exhausted.\n");
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
