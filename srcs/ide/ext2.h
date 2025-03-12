#ifndef EXT2_H
#define EXT2_H

#include "../utils/stdint.h"

/* Constants and sizes */
#define EXT2_BLOCK_SIZE    1024
#define EXT2_INODE_SIZE    128
#define EXT2_ROOT_INODE    2
#define EXT2_PARTITION_START 0  /* starting LBA of the ext2 partition */

struct ext2_super_block
{
    uint32_t s_inodes_count;
    uint32_t s_blocks_count;
    uint32_t s_r_blocks_count;
    uint32_t s_free_blocks_count;
    uint32_t s_free_inodes_count;
    uint32_t s_first_data_block;
    uint32_t s_log_block_size;
    uint32_t s_log_frag_size;
    uint32_t s_blocks_per_group;
    uint32_t s_frags_per_group;
    uint32_t s_inodes_per_group;
    uint32_t s_mtime;
    uint32_t s_wtime;
    uint16_t s_mnt_count;
    uint16_t s_max_mnt_count;
    uint16_t s_magic;
    uint16_t s_state;
    uint16_t s_errors;
    uint16_t s_minor_rev_level;
    uint32_t s_lastcheck;
    uint32_t s_checkinterval;
    uint32_t s_creator_os;
    uint32_t s_rev_level;
    uint16_t s_def_resuid;
    uint16_t s_def_resgid;
    /* … additional fields omitted … */
};

struct ext2_group_desc
{
    uint32_t bg_block_bitmap;
    uint32_t bg_inode_bitmap;
    uint32_t bg_inode_table;
    uint16_t bg_free_blocks_count;
    uint16_t bg_free_inodes_count;
    uint16_t bg_used_dirs_count;
    uint16_t bg_pad;
    uint32_t bg_reserved[3];
};

struct ext2_inode
{
    uint16_t i_mode;         /* file mode */
    uint16_t i_uid;          /* low 16 bits of owner uid */
    uint32_t i_size;         /* size in bytes */
    uint32_t i_atime;        /* access time */
    uint32_t i_ctime;        /* creation time */
    uint32_t i_mtime;        /* modification time */
    uint32_t i_dtime;        /* deletion time */
    uint16_t i_gid;          /* low 16 bits of group id */
    uint16_t i_links_count;  /* links count */
    uint32_t i_blocks;       /* blocks count in 512-byte units */
    uint32_t i_flags;        /* file flags */
    uint32_t i_osd1;         /* OS dependent 1 */
    uint32_t i_block[15];    /* pointers to blocks */
    uint32_t i_generation;   /* file version (for NFS) */
    uint32_t i_file_acl;     /* file ACL */
    uint32_t i_dir_acl;      /* directory ACL */
    uint32_t i_faddr;        /* fragment address */
    uint8_t  i_osd2[12];     /* OS dependent 2 */
};

struct ext2_dir_entry
{
    uint32_t inode;          /* inode number */
    uint16_t rec_len;        /* record length */
    uint8_t  name_len;       /* name length */
    uint8_t  file_type;      /* file type */
    char     name[255];      /* file name (not null terminated!) */
};

#define EXT2_FT_UNKNOWN  0
#define EXT2_FT_REG_FILE 1
#define EXT2_FT_DIR      2

/* Command interfaces */
void ext2_init(void);
void ext2_cmd_ls(const char* path);
void ext2_cmd_cat(const char* path);
void ext2_cmd_touch(const char* path);
void ext2_cmd_mkdir(const char* path);
void ext2_cmd_rm(const char* path);
void ext2_cmd_rmdir(const char* path);
void ext2_cmd_cd(const char* path);
uint32_t ext2_get_inode(const char *path);

void set_current_dir(uint32_t inode);

int ext2_remove_all_files(const char *dir_path);

#endif
