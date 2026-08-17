#ifndef EXT2_FILEIO_H
#define EXT2_FILEIO_H

#include "../utils/stdint.h"
#include "../utils/utils.h"
#include "../sockets/socket.h"
#include "../modules/modules.h"
#include "ext2.h"

#define MAX_FDS 32

#define O_RDONLY    00
#define O_WRONLY    01
#define O_RDWR      02
#define O_CREAT     0100
#define O_EXCL      0200
#define O_NOCTTY    0400
#define O_TRUNC     01000
#define O_APPEND    02000
#define O_NONBLOCK  04000
#define O_DIRECTORY 0200000
#define O_CLOEXEC   02000000
#define FILE_MODE    0x8000   // Regular file (S_IFREG)
#define DIR_MODE     0x4000   // Directory (S_IFDIR)
#define DEVICE_MODE  0x2000   // Character device (S_IFCHR)

#define EBADF 9
#define EFAULT 14
#define EMFILE 24

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

struct task_struct;
typedef struct task_struct task_t;

typedef struct ext2_FILE
{
    uint32_t inode_num;         /* which inode this file refers to */
    struct ext2_inode inode;    /* cached inode */
    uint32_t pos;               /* current read/write offset */
    int mode;                   /* 0=read, 1=write (for simplicity) */
} ext2_FILE;

typedef enum { FD_FILE = 0, FD_SOCKET, FD_MODULE, FD_TTY, FD_DIR, FD_PROC_DIR, FD_NULL } fd_type_t;

/* With fileops i can directly dispatch the read/write/close operations
 * allowing me to use the same struct for files, modules, stdin/stdout, etc.
 */
typedef struct file_operations
{
    ssize_t (*read)(void* fp, void *buf, size_t count);
    ssize_t (*write)(void* fp, const void *buf, size_t count);
    int     (*close)(void* fp);
} file_operations_t;

typedef struct file
{
    file_operations_t fops;
    fd_type_t type;
    int flags;
    int ref_count;
    uint32_t offset;
    void* fp;
    // union
    // {
    //     ext2_FILE*  file;
    //     socket_t*   socket;
    //     module_t*   module;
    // };
} file_t;

ext2_FILE *ext2_fopen(const char *path, const char *mode);
size_t ext2_fread(ext2_FILE *stream, void *ptr, size_t size);
size_t ext2_fwrite(ext2_FILE *stream, const void *ptr, size_t size);
int ext2_fclose(ext2_FILE *stream);

int sys_open(const char *path, int flags);
int sys_chmod(const char *path, int mode);
int sys_chdir(const char *path);
int sys_mkdir(const char *path, uint32_t mode);
int sys_rmdir(const char *path);
int sys_rename(const char *oldpath, const char *newpath);
int sys_link(const char *oldpath, const char *newpath);
int sys_unlink(const char *path);

struct kfs_statfs64
{
    uint32_t f_type;
    uint32_t f_bsize;
    uint64_t f_blocks;
    uint64_t f_bfree;
    uint64_t f_bavail;
    uint64_t f_files;
    uint64_t f_ffree;
    uint32_t f_fsid[2];
    uint32_t f_namelen;
    uint32_t f_frsize;
    uint32_t f_flags;
    uint32_t f_spare[4];
};
int sys_statfs64(const char *path, uint32_t bufsize, struct kfs_statfs64 *buf);
int sys_close(int fd, task_t* current);
ssize_t sys_read(int fd, void *buf, size_t count);
ssize_t sys_lseek(int fd, ssize_t offset, int whence);
ssize_t sys_write(int fd, const void *buf, size_t count);
ssize_t sys_getdents64(int fd, void *dirp, size_t count);


int create_device_node(const char *dir, const char *name, module_t *module);
int delete_device_node(const char *dir, const char *name);

#endif
