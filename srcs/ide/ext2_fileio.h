#ifndef EXT2_FILEIO_H
#define EXT2_FILEIO_H

#include "../utils/stdint.h"
#include "../utils/utils.h"
#include "../sockets/socket.h"
#include "../modules/modules.h"
#include "ext2.h"

#define MAX_FDS 32
#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
#define O_CREAT 4
#define O_TRUNC 8
#define O_APPEND 16
#define FILE_MODE    0x8000   // Regular file (S_IFREG)
#define DIR_MODE     0x4000   // Directory (S_IFDIR)
#define DEVICE_MODE  0x2000   // Character device (S_IFCHR)

#define EBADF 9
#define EFAULT 14
#define EMFILE 24

typedef struct ext2_FILE
{
    uint32_t inode_num;         /* which inode this file refers to */
    struct ext2_inode inode;    /* cached inode */
    uint32_t pos;               /* current read/write offset */
    int mode;                   /* 0=read, 1=write (for simplicity) */
} ext2_FILE;

typedef enum { FD_FILE = 0, FD_SOCKET, FD_MODULE } fd_type_t;

/* Not being used for now but may be interesting */
// typedef struct file_operations
// {
//     ssize_t (*read)(struct file *file, void *buf, size_t count);
//     ssize_t (*write)(struct file *file, const void *buf, size_t count);
//     int (*close)(struct file *file);
// } file_operations_t;

typedef struct file
{
    // file_operations_t *fops;
    fd_type_t type;
    int flags;
    int ref_count;
    uint32_t offset;
    union
    {
        ext2_FILE*  file;
        socket_t*   socket;
        module_t*   module;
    };
} file_t;

ext2_FILE *ext2_fopen(const char *path, const char *mode);
size_t ext2_fread(void *ptr, size_t size, size_t nmemb, ext2_FILE *stream);
size_t ext2_fwrite(const void *ptr, size_t size, size_t nmemb, ext2_FILE *stream);
int ext2_fclose(ext2_FILE *stream);

int sys_open(const char *path, int flags);
int sys_close(int fd);
ssize_t sys_read(int fd, void *buf, size_t count);
ssize_t sys_write(int fd, const void *buf, size_t count);


int create_device_node(const char *dir, const char *name, module_t *module);

#endif
