#include "socket.h"
#include "../tasks/task.h"
#include "../utils/utils.h"
#include "../ide/ext2_fileio.h"

socket_t *socket_create(int domain, int type, int protocol)
{
    socket_t *sock = kmalloc(sizeof(socket_t));
    if (!sock) return NULL;
    sock->domain = domain;
    sock->type = type;
    sock->protocol = protocol;
    sock->peer = NULL;
    sock->head = 0;
    sock->tail = 0;
    sock->count = 0;
    memset(sock->buffer, 0, SOCKET_BUFFER_SIZE);
    return sock;
}

int socket_connect(socket_t *sock, socket_t *peer)
{
    if (!sock || !peer) return -1;
    sock->peer = peer;
    peer->peer = sock;
    return 0;
}

ssize_t socket_send(socket_t *sock, const void *buf, size_t len)
{
    socket_t *peer;
    size_t sent;

    if (!sock || !sock->peer)
        return -1;

    peer = sock->peer;
    sent = 0;

    while (sent < len && peer->count < SOCKET_BUFFER_SIZE)
    {
        peer->buffer[peer->tail] = ((const char*)buf)[sent];
        peer->tail = (peer->tail + 1) % SOCKET_BUFFER_SIZE;
        peer->count++;
        sent++;
    }
    return sent;
}

ssize_t socket_recv(socket_t *sock, void *buf, size_t len)
{
    if (!sock)
        return -1;

    size_t recvd = 0;

    while (recvd < len && sock->count > 0)
    {
        ((char*)buf)[recvd] = sock->buffer[sock->head];
        sock->head = (sock->head + 1) % SOCKET_BUFFER_SIZE;
        sock->count--;
        recvd++;
    }
    return recvd;
}

void socket_close(socket_t *sock)
{
    if (!sock) return;
    if (sock->peer)
    {
        /* Disconnect peer */
        sock->peer->peer = NULL;
    }
    kfree(sock);
}

int sys_socket(int domain, int type, int protocol)
{
    task_t *current;
    int fd;
    socket_t *sock;

    current = get_current_task();
    for (fd = 0; fd < MAX_FDS; fd++)
    {
        if (current->fd_table[fd] == false)
            break;
    }
    if (fd == MAX_FDS)
    {
        printf("sys_socket: too many open files\n");
        return -1;
    }
    
    sock = socket_create(domain, type, protocol);
    if (!sock)
        return -1;
    
    /* TODO REVIEWWWW!!! */
    current->fd_pointers[fd].fops.read = (void*)socket_recv;
    current->fd_pointers[fd].fops.write = (void*)socket_send;
    current->fd_pointers[fd].fops.close = (void*)socket_close;
    /* TODO REVIEWWWW!!! */
    current->fd_pointers[fd].flags = 0;
    current->fd_pointers[fd].fp = sock;
    current->fd_pointers[fd].type = FD_SOCKET;
    current->fd_pointers[fd].ref_count = 1;
    current->fd_table[fd] = true;

    return fd;
}

int sys_bind(int sockfd, const char *address)
{
    task_t *current = get_current_task();
    if (sockfd < 0 || sockfd >= MAX_FDS || current->fd_table[sockfd] == false)
        return -1;

    file_t *file_obj = &current->fd_pointers[sockfd];
    if (file_obj->type != FD_SOCKET)
    {
        printf("sys_bind: fd %d is not a socket\n", sockfd);
        return -1;
    }
    
    registry_insert(address, file_obj->fp);
    return 0;
}

ssize_t sys_recv(int fd, void *buf, size_t count)
{
    task_t *current;
    file_t *file_obj;

    current = get_current_task();
    if (fd < 0 || fd >= MAX_FDS || current->fd_table[fd] == false)
        return -1;

     file_obj = &current->fd_pointers[fd];
    if (file_obj->type != FD_SOCKET) {
        printf("sys_recv: fd %d is not a socket\n", fd);
        return -1;
    }
    return socket_recv(file_obj->fp, buf, count);
}

ssize_t sys_send(int fd, const void *buf, size_t count)
{
    task_t *current;
    file_t *file_obj;

    current = get_current_task();
    if (fd < 0 || fd >= MAX_FDS || current->fd_table[fd] == false)
        return -1;

    file_obj = &current->fd_pointers[fd];
    if (file_obj->type != FD_SOCKET) {
        printf("sys_send: fd %d is not a socket\n", fd);
        return -1;
    }
    return socket_send(file_obj->fp, buf, count);
}


int sys_connect(const char *address)
{
    socket_t *peer = registry_lookup(address);
    if (!peer)
    {
        printf("sys_connect: address not found: %s\n", address);
        return -1;
    }

    task_t *current = get_current_task();
    int fd;
    for (fd = 0; fd < MAX_FDS; fd++)
    {
        if (current->fd_table[fd] == false)
            break;
    }
    if (fd == MAX_FDS)
    {
        printf("sys_connect: too many open files\n");
        return -1;
    }

    socket_t *client_sock = socket_create(peer->domain, peer->type, peer->protocol);
    if (!client_sock) return -1;

    if (socket_connect(client_sock, peer) < 0)
    {
        printf("sys_connect: failed to connect\n");
        kfree(client_sock);
        return -1;
    }

    file_t *file_obj = kmalloc(sizeof(file_t));
    file_obj->type = FD_SOCKET;
    file_obj->fp = client_sock;
    file_obj->flags = 0;
    file_obj->ref_count = 1;

    file_obj->fops.read = (void*)socket_recv;
    file_obj->fops.write = (void*)socket_send;
    file_obj->fops.close = (void*)socket_close;

    current->fd_pointers[fd] = *file_obj;
    current->fd_table[fd] = true;
    kfree(file_obj);
    return fd;
}
