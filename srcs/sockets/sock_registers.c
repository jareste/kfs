#include "sock_registers.h"
#include "../memory/memory.h"
#include "../utils/utils.h"

static socket_registry_node_t *socket_registry = NULL;

void registry_insert(const char *address, socket_t *sock)
{
    socket_registry_node_t *node = kmalloc(sizeof(socket_registry_node_t));
    node->address = kstrdup(address);
    node->sock = sock;
    node->next = socket_registry;
    socket_registry = node;
}

socket_t *registry_lookup(const char *address)
{
    socket_registry_node_t *node = socket_registry;
    while (node) {
        if (strcmp(node->address, address) == 0)
            return node->sock;
        node = node->next;
    }
    return NULL;
}

void registry_remove(const char *address)
{
    socket_registry_node_t **curr = &socket_registry;
    while (*curr) {
        if (strcmp((*curr)->address, address) == 0) {
            socket_registry_node_t *to_delete = *curr;
            *curr = (*curr)->next;
            kfree(to_delete->address);
            kfree(to_delete);
            return;
        }
        curr = &((*curr)->next);
    }
}
