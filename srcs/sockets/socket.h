#ifndef SOCKET_H
#define SOCKET_H

#include "../utils/stdint.h"
#include "../memory/memory.h"
#include "../utils/utils.h"
#include "sock_registers.h"

#define SOCKET_BUFFER_SIZE 1024
#define AF_UNIX 1
#define AF_LOCAL 2
#define AF_INET 3
#define AF_INET6 4
#define AF_IPX 5
#define AF_NETLINK 6
#define AF_X25 9
#define AF_AX25 10
#define AF_ATMPVC 11
#define AF_APPLETALK 16
#define AF_PACKET 17
#define AF_ALG 18

#define SOCK_STREAM 1
#define SOCK_DGRAM 2
#define SOCK_SEQPACKET 3
#define SOCK_RAW 4
#define SOCK_RDM 5
#define SOCK_PACKET 6


typedef struct socket
{
    int domain; /* FOO for now */
    int type; /* FOO for now */
    int protocol; /* Foo for the moment, but usefull for setting TCP/UDP/whatever */
    struct socket *peer; 
    char buffer[SOCKET_BUFFER_SIZE];
    int head; /* index for reading data */
    int tail; /* index for writing data */
    int count; /* number of bytes currently in the buffer */

    /* TODO: add locks/wait queues */
} socket_t;

typedef struct socket_registry_node
{
    char *address;
    socket_t *sock;
    struct socket_registry_node *next;
} socket_registry_node_t;

socket_t *socket_create(int domain, int type, int protocol);
int sys_socket(int domain, int type, int protocol);

int socket_connect(socket_t *sock, socket_t *peer);

ssize_t socket_send(socket_t *sock, const void *buf, size_t len);

ssize_t socket_recv(socket_t *sock, void *buf, size_t len);

void socket_close(socket_t *sock);

int sys_bind(int sockfd, const char *address);
int sys_connect(const char *address);

void registry_insert(const char *address, socket_t *sock);
socket_t *registry_lookup(const char *address);
void registry_remove(const char *address);

#endif
