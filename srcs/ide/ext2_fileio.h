#ifndef EXT2_FILEIO_H
#define EXT2_FILEIO_H

#include "../utils/stdint.h"
#include "../utils/utils.h"
#include "ext2.h"

#define MAX_FDS 32
#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
#define O_CREAT 4
#define O_TRUNC 8
#define O_APPEND 16

typedef struct ext2_FILE
{
    uint32_t inode_num;         /* which inode this file refers to */
    struct ext2_inode inode;    /* cached inode */
    uint32_t pos;               /* current read/write offset */
    int mode;                   /* 0=read, 1=write (for simplicity) */
} ext2_FILE;

typedef struct file
{
    int flags;           // e.g., O_RDONLY, O_WRONLY, etc.
    ext2_FILE *fp;       // our ext2 file structure from your file I/O layer
    uint32_t offset;     // current offset (could mirror fp->pos)
    int ref_count;       // reference count (useful if fds are shared between processes)
} file_t;

ext2_FILE *ext2_fopen(const char *path, const char *mode);
size_t ext2_fread(void *ptr, size_t size, size_t nmemb, ext2_FILE *stream);
size_t ext2_fwrite(const void *ptr, size_t size, size_t nmemb, ext2_FILE *stream);
int ext2_fclose(ext2_FILE *stream);

#endif
