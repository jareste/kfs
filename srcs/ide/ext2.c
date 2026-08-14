#include "ext2.h"
#include "../ide/ide.h"
#include "../memory/memory.h"
#include "../memory/kmalloc.h"
#include "../utils/utils.h"
#include "../utils/stdint.h"
#include "../kshell/kshell.h"
#include "../keyboard/keyboard.h"
#include "../display/display.h"
#include "../tasks/task.h"
#include "../panic/kpanic.h"
#include "ext2_fileio.h"


/* --- Derived constants --- */
#define SECTORS_PER_BLOCK (EXT2_BLOCK_SIZE / IDE_SECTOR_SIZE)
#define EXT2_SUPER_MAGIC 0xEF53

/* --- Global FS structure --- */
struct ext2_fs
{
    struct ext2_super_block sb;
    struct ext2_group_desc  gd;
    uint8_t* inode_bitmap;  /* one block */
    uint8_t* block_bitmap;  /* one block */
} ext2;

typedef struct
{
    char  *data;
    size_t len;
    size_t pos;
} proc_buf_t;

static uint32_t current_dir = EXT2_ROOT_INODE;  /* current working directory inode */

void set_current_dir(uint32_t inode)
{
    current_dir = inode;
}

static void split_path(const char *full_path, char *out_parent, char *out_name)
{
    char temp[256];
    strcpy(temp, full_path);

    char *last_slash = strrchr(temp, '/');
    if (!last_slash)
    {
        strcpy(out_parent, ".");
        strcpy(out_name, temp);
        return;
    }

    *last_slash = '\0';
    if (last_slash == temp)
    {
        strcpy(out_parent, "/");
    }
    else
    {
        strcpy(out_parent, temp);
    }
    strcpy(out_name, last_slash + 1);
    if (out_name[0] == '\0')
    {
        strcpy(out_name, ".");
    }
}

static void ext2_read_block(uint32_t block, void *buf)
{
    uint32_t lba = EXT2_PARTITION_START + block * SECTORS_PER_BLOCK;
    if (ide_read_sectors(lba, SECTORS_PER_BLOCK, buf) < 0)
        kpanic("ext2: read block error", 1);
}

static void ext2_write_block(uint32_t block, void *buf)
{
    uint32_t lba = EXT2_PARTITION_START + block * SECTORS_PER_BLOCK;
    if (ide_write_sectors(lba, SECTORS_PER_BLOCK, buf) < 0)
        kpanic("ext2: write block error", 1);
}

static void ext2_read_inode(uint32_t inode_num, struct ext2_inode *inode)
{
    uint32_t index = inode_num - 1;
    uint32_t block_offset = (index * EXT2_INODE_SIZE) / EXT2_BLOCK_SIZE;
    uint32_t offset_in_block = (index * EXT2_INODE_SIZE) % EXT2_BLOCK_SIZE;
    uint8_t* block = kmalloc(EXT2_BLOCK_SIZE);
    ext2_read_block(ext2.gd.bg_inode_table + block_offset, block);
    memcpy(inode, block + offset_in_block, sizeof(struct ext2_inode));
    kfree(block);
}

static void ext2_write_inode(uint32_t inode_num, struct ext2_inode *inode)
{
    uint32_t index = inode_num - 1;
    uint32_t block_offset = (index * EXT2_INODE_SIZE) / EXT2_BLOCK_SIZE;
    uint32_t offset_in_block = (index * EXT2_INODE_SIZE) % EXT2_BLOCK_SIZE;
    uint8_t* block = kmalloc(EXT2_BLOCK_SIZE);
    ext2_read_block(ext2.gd.bg_inode_table + block_offset, block);
    memcpy(block + offset_in_block, inode, sizeof(struct ext2_inode));
    ext2_write_block(ext2.gd.bg_inode_table + block_offset, block);
    kfree(block);
}

static uint32_t ext2_allocate_inode(void)
{
    uint32_t total = ext2.sb.s_inodes_count;
    for (uint32_t i = 0; i < total; i++)
    {
        uint32_t byte = i / 8;
        uint8_t bit = 1 << (i % 8);
        if (!(ext2.inode_bitmap[byte] & bit))
        {
            ext2.inode_bitmap[byte] |= bit;
            ext2_write_block(ext2.gd.bg_inode_bitmap, ext2.inode_bitmap);
            return i + 1;
        }
    }
    return 0;
}

static uint32_t ext2_allocate_block(void)
{
    uint32_t total = ext2.sb.s_blocks_count;
    for (uint32_t i = 0; i < total; i++)
    {
        uint32_t byte = i / 8;
        uint8_t bit = 1 << (i % 8);
        if (!(ext2.block_bitmap[byte] & bit))
        {
            ext2.block_bitmap[byte] |= bit;
            ext2_write_block(ext2.gd.bg_block_bitmap, ext2.block_bitmap);
            return i + 1;
        }
    }
    return 0;
}

static void ext2_free_inode(uint32_t inode_num)
{
    uint32_t index = inode_num - 1;
    uint32_t byte = index / 8;
    uint8_t bit = 1 << (index % 8);
    ext2.inode_bitmap[byte] &= ~bit;
    ext2_write_block(ext2.gd.bg_inode_bitmap, ext2.inode_bitmap);
}

static void ext2_free_block(uint32_t block)
{
    uint32_t index = block - 1;
    uint32_t byte = index / 8;
    uint8_t bit = 1 << (index % 8);
    ext2.block_bitmap[byte] &= ~bit;
    ext2_write_block(ext2.gd.bg_block_bitmap, ext2.block_bitmap);
}

static int ext2_add_dir_entry(uint32_t parent_inode_num, const char *name,
                              uint32_t inode_num, uint8_t file_type)
{
    struct ext2_inode parent;
    ext2_read_inode(parent_inode_num, &parent);
    uint32_t block;
    if (parent.i_block[0] == 0)
    {
        block = ext2_allocate_block();
        if (block == 0)
        {
            kprintf("No free block available\n");
            return -1;
        }
        parent.i_block[0] = block;
        parent.i_blocks = 2;  /* block count in 512-byte units */
        parent.i_size = EXT2_BLOCK_SIZE;
        ext2_write_inode(parent_inode_num, &parent);
        uint8_t* buf = kmalloc(EXT2_BLOCK_SIZE);
        memset(buf, 0, EXT2_BLOCK_SIZE);
        ext2_write_block(block, buf);
        kfree(buf);
    }
    else
    {
        block = parent.i_block[0];
    }
    
    uint8_t* blkbuf = kmalloc(EXT2_BLOCK_SIZE);
    ext2_read_block(block, blkbuf);
    int offset = 0;
    struct ext2_dir_entry *de;
    while (offset < EXT2_BLOCK_SIZE)
    {
        de = (struct ext2_dir_entry *)(blkbuf + offset);
        if (de->inode == 0)
            break;
        /* compute ideal length for this entry */
        int ideal = ((8 + de->name_len + 3) / 4) * 4;
        int avail = de->rec_len - ideal;
        if (avail >= ((8 + strlen(name) + 3) / 4) * 4)
        {
            de->rec_len = ideal;
            offset += ideal;
            de = (struct ext2_dir_entry *)(blkbuf + offset);
            de->inode = inode_num;
            de->rec_len = avail;
            de->name_len = strlen(name);
            de->file_type = file_type;
            memcpy(de->name, name, de->name_len);
            break;
        }
        offset += de->rec_len;
    }
    if (offset >= EXT2_BLOCK_SIZE)
    {
        kprintf("Directory full, cannot add entry\n");
        kfree(blkbuf);
        return -1;
    }
    ext2_write_block(block, blkbuf);
    kfree(blkbuf);
    return 0;
}

static int ext2_remove_dir_entry(uint32_t parent_inode_num, const char *name)
{
    struct ext2_inode parent;
    ext2_read_inode(parent_inode_num, &parent);
    uint8_t* blkbuf = kmalloc(EXT2_BLOCK_SIZE);
    ext2_read_block(parent.i_block[0], blkbuf);
    int offset = 0;
    struct ext2_dir_entry *prev = NULL, *de = NULL;
    int found = 0;
    while (offset < EXT2_BLOCK_SIZE)
    {
        de = (struct ext2_dir_entry *)(blkbuf + offset);
        if (de->inode != 0)
        {
            char tmp[256];
            memcpy(tmp, de->name, de->name_len);
            tmp[de->name_len] = '\0';
            if (strcmp(tmp, name) == 0) { found = 1; break; }
        }
        prev = de;
        offset += de->rec_len;
    }
    if (!found)
    {
        kfree(blkbuf);
        return -1;
    }
    if (prev == NULL)
    {
        de->inode = 0;
    }
    else
    {
        prev->rec_len += de->rec_len;
    }
    ext2_write_block(parent.i_block[0], blkbuf);
    kfree(blkbuf);
    return 0;
}

static int ext2_lookup(uint32_t dir_inode_num, const char *name, uint32_t *child)
{
    struct ext2_inode dir;
    ext2_read_inode(dir_inode_num, &dir);
    if (!(dir.i_mode & DIR_MODE))
    {  /* not a directory */
        kprintf("Not a directory\n");
        return -1;
    }
    uint8_t* blkbuf = kmalloc(EXT2_BLOCK_SIZE);
    ext2_read_block(dir.i_block[0], blkbuf);
    int offset = 0;
    struct ext2_dir_entry *de;
    while (offset < EXT2_BLOCK_SIZE)
    {
        de = (struct ext2_dir_entry *)(blkbuf + offset);
        if (de->inode != 0)
        {
            char tmp[256];
            memcpy(tmp, de->name, de->name_len);
            tmp[de->name_len] = '\0';
            if (strcmp(tmp, name) == 0)
            {
                *child = de->inode;
                kfree(blkbuf);
                return 0;
            }
        }
        offset += de->rec_len;
    }
    kfree(blkbuf);
    return -1;
}

static int ext2_resolve_path(const char *path, uint32_t *inode_out)
{
    uint32_t cur = (path[0]=='/') ? EXT2_ROOT_INODE : current_dir;
    char token[256];
    const char *p = path;
    if (path[0]=='/') p++;
    while (*p)
    {
        int i = 0;
        while (*p && *p != '/') token[i++] = *p++;
        token[i] = '\0';
        if (i == 0) break;
        uint32_t child;
        if (ext2_lookup(cur, token, &child) < 0) return -1;
        cur = child;
        if (*p) p++;
    }
    *inode_out = cur;
    return 0;
}

int ext2_remove_all_files(const char *dir_path)
{
    uint32_t dir_inode_num;
    if (ext2_resolve_path(dir_path, &dir_inode_num) < 0)
    {
        kprintf("ext2_remove_all_files: directory not found: %s\n", dir_path);
        return -1;
    }

    struct ext2_inode dir_inode;
    ext2_read_inode(dir_inode_num, &dir_inode);
    if (!(dir_inode.i_mode & DIR_MODE))
    {
        kprintf("ext2_remove_all_files: %s is not a directory\n", dir_path);
        return -1;
    }

    if (dir_inode.i_block[0] == 0)
    {
        return 0;
    }

    uint8_t *blockbuf = kmalloc(EXT2_BLOCK_SIZE);
    ext2_read_block(dir_inode.i_block[0], blockbuf);

    char *file_names[32];
    int file_count = 0;
    int offset = 0;

    while (offset < EXT2_BLOCK_SIZE)
    {
        struct ext2_dir_entry *de = (struct ext2_dir_entry *)(blockbuf + offset);
        if (de->inode != 0)
        {
            char name[256];
            memcpy(name, de->name, de->name_len);
            name[de->name_len] = '\0';
            if (strcmp(name, ".") != 0 && strcmp(name, "..") != 0)
            {
                if (de->file_type == EXT2_FT_REG_FILE)
                {
                    if (file_count < 32)
                        file_names[file_count++] = kstrdup(name);
                    else
                    {
                        kprintf("ext2_remove_all_files: too many files; increase array size\n");
                        break;
                    }
                }
            }
        }
        offset += de->rec_len;
    }
    kfree(blockbuf);

    for (int i = 0; i < file_count; i++)
    {
        char full_path[256];
        strcpy(full_path, dir_path);
        if (dir_path[strlen(dir_path) - 1] != '/')
            strcat(full_path, "/");
        strcat(full_path, file_names[i]);
        ext2_cmd_rm(full_path);
        kfree(file_names[i]);
    }

    return 0;
}

static int ext2_create_file(uint32_t parent_inode_num, const char *name, uint16_t mode)
{
    uint32_t new_inode = ext2_allocate_inode();
    if (new_inode == 0)
    {
        kprintf("No free inode available\n");
        return -1;
    }
    struct ext2_inode file;
    memset(&file, 0, sizeof(file));
    file.i_mode = mode; /* e.g. 0x8000 for regular file */
    file.i_size = 0;
    file.i_links_count = 1;
    file.i_blocks = 0;
    ext2_write_inode(new_inode, &file);
    if (ext2_add_dir_entry(parent_inode_num, name, new_inode, (mode & DIR_MODE) ? EXT2_FT_DIR : EXT2_FT_REG_FILE) < 0)
        return -1;
    return new_inode;
}

static void ext2_truncate_inode(uint32_t inode_num, struct ext2_inode *inode)
{
    if (inode->i_block[0] != 0)
    {
        ext2_free_block(inode->i_block[0]);
        inode->i_block[0] = 0;
    }
    inode->i_size = 0;
    inode->i_blocks = 0;
    ext2_write_inode(inode_num, inode);
}

/* Get inode from path */
uint32_t ext2_get_inode(const char *path)
{
    uint32_t inode_num;
    if (ext2_resolve_path(path, &inode_num) < 0)
    {
        kprintf("ext2_get_inode: path not found: %s\n", path);
        return 0;
    }
    return inode_num;
}

char *ext2_pwd(void)
{
    if (current_dir == EXT2_ROOT_INODE)
        return kstrdup("/");

    char *components[32];
    int count = 0;
    uint32_t curr = current_dir;

    while (curr != EXT2_ROOT_INODE)
    {
        struct ext2_inode curr_inode;
        ext2_read_inode(curr, &curr_inode);

        if (curr_inode.i_block[0] == 0)
            break;

        uint8_t *buf = kmalloc(EXT2_BLOCK_SIZE);
        ext2_read_block(curr_inode.i_block[0], buf);

        struct ext2_dir_entry *dot = (struct ext2_dir_entry *)buf;
        struct ext2_dir_entry *dotdot = (struct ext2_dir_entry *)(buf + dot->rec_len);
        uint32_t parent = dotdot->inode;
        kfree(buf);

        if (parent == curr)
            break;

        struct ext2_inode parent_inode;
        ext2_read_inode(parent, &parent_inode);
        if (parent_inode.i_block[0] == 0)
            break;
        uint8_t *pbuf = kmalloc(EXT2_BLOCK_SIZE);
        ext2_read_block(parent_inode.i_block[0], pbuf);

        int offset = 0;
        char name[256];
        int found = 0;
        while (offset < EXT2_BLOCK_SIZE)
        {
            struct ext2_dir_entry *entry = (struct ext2_dir_entry *)(pbuf + offset);
            if (entry->inode == curr && entry->name_len > 0 &&
                strcmp(entry->name, ".") != 0 &&
                strcmp(entry->name, "..") != 0)
            {
                memcpy(name, entry->name, entry->name_len);
                name[entry->name_len] = '\0';
                found = 1;
                break;
            }
            offset += entry->rec_len;
        }
        kfree(pbuf);

        if (!found)
        {
            /* Fall back: convert inode number to string */
            uitoa(curr, name, 10);
        }
        components[count++] = kstrdup(name);
        curr = parent;
        if (curr == EXT2_ROOT_INODE)
            break;
    }

    char *pwd = kmalloc(256);
    strcpy(pwd, "/");
    for (int i = count - 1; i >= 0; i--) 
    {
        strcat(pwd, components[i]);
        if (i > 0)
            strcat(pwd, "/");
        kfree(components[i]);
    }
    return pwd;
}

ext2_FILE *ext2_fopen(const char *path, const char *mode)
{
    int allow_write = 0;
    int truncate = 0;
    int append = 0;
    if (strcmp(mode, "r") == 0)
    {
        allow_write = 0;
    }
    else if (strcmp(mode, "w") == 0)
    {
        allow_write = 1;
        truncate = 1;
    }
    else if (strcmp(mode, "r+") == 0)
    {
        allow_write = 1;
        /* no truncation */
    }
    else if (strcmp(mode, "a") == 0)
    {
        allow_write = 1;
        append = 1;
    }
    else
    {
        kprintf("ext2_fopen: unsupported mode '%s'\n", mode);
        return NULL;
    }

    uint32_t inode_num;
    if (ext2_resolve_path(path, &inode_num) < 0)
    {
        if (!allow_write)
        {
            return NULL;
        }
        
        char parent_path[256], file_name[256];
        split_path(path, parent_path, file_name);
        uint32_t parent_inode;
        if (ext2_resolve_path(parent_path, &parent_inode) < 0)
        {
            kprintf("ext2_fopen: parent directory not found '%s'\n", parent_path);
            return NULL;
        }
        uint32_t new_inode = ext2_create_file(parent_inode, file_name, FILE_MODE);
        if (!new_inode)
        {
            kprintf("ext2_fopen: cannot create file '%s'\n", path);
            return NULL;
        }
        inode_num = new_inode;
    }

    struct ext2_inode in;
    ext2_read_inode(inode_num, &in);
    if (in.i_mode & DIR_MODE)
    {
        kprintf("ext2_fopen: '%s' is a directory\n", path);
        return NULL;
    }

    if (truncate)
    {
        ext2_truncate_inode(inode_num, &in);
    }

    ext2_FILE *fp = kmalloc(sizeof(ext2_FILE));
    fp->inode_num = inode_num;
    fp->inode = in;
    fp->pos = 0;
    fp->mode = allow_write;

    if (append)
    {
        fp->pos = fp->inode.i_size;
    }

    return fp;
}

int ext2_fclose(ext2_FILE *stream)
{
    if (!stream) return -1;
    kfree(stream);
    return 0;
}

size_t ext2_fread(ext2_FILE *stream, void *ptr, size_t size)
{
    // uint8_t blockbuf[EXT2_BLOCK_SIZE];
    uint8_t *blockbuf = kmalloc(EXT2_BLOCK_SIZE);
    size_t bytes_read;
    uint8_t *out;
    size_t remain;
    size_t to_copy;
    uint32_t block_num;
    uint32_t file_offset;
    uint32_t block_index;
    uint32_t block_offset;
    uint32_t indirect_index;

    if (!stream || stream->mode != 0)
        return 0;
    if (size == 0)
        return 0;
    if (stream->pos >= stream->inode.i_size)
        return 0;

    remain = stream->inode.i_size - stream->pos;
    if (remain < size)
        size = remain;


    int test_yes = 0;
    if (test_yes == 3)
    {
    kprintf("[EXT2] inode=%d size=%d\n", stream->inode_num, stream->inode.i_size);
    kprintf("[EXT2] i_block: ");
    for (int i = 0; i < 15; i++)
        kprintf("%d ", stream->inode.i_block[i]);
    kprintf("\n");

    kprintf("[EXT2] i_blocks=%d (en unidades de 512 bytes = %d KB)\n",
        stream->inode.i_blocks,
        stream->inode.i_blocks / 2);

        uint32_t *test_indirect = kmalloc(EXT2_BLOCK_SIZE);
        ext2_read_block(stream->inode.i_block[12], (uint8_t*)test_indirect);
        kprintf("[EXT2] indirect block 220, first entries: %d %d %d %d\n",
                test_indirect[0], test_indirect[1], test_indirect[2], test_indirect[3]);
        kfree(test_indirect);
    }
    out = (uint8_t*)ptr;
    bytes_read = 0;
    while (bytes_read < size)
    {
        file_offset = stream->pos + bytes_read;
        block_index = file_offset / EXT2_BLOCK_SIZE;
        block_offset = file_offset % EXT2_BLOCK_SIZE;

        block_num = 0;
        // if (block_index < 12)
        // {
        //     block_num = stream->inode.i_block[block_index];
        // }
        // else
        // {
        //     indirect_index = block_index - 12;
        //     if (indirect_index < EXT2_BLOCK_SIZE / 4)
        //     {
        //         uint32_t indirect_buf[EXT2_BLOCK_SIZE / 4];
        //         ext2_read_block(stream->inode.i_block[12], (uint8_t*)indirect_buf);
        //         block_num = indirect_buf[indirect_index];
        //     }
        // }
        if (block_index < 12) {
            block_num = stream->inode.i_block[block_index];
        }
        else if (block_index < 12 + 256) {
            // Indirección simple
            indirect_index = block_index - 12;
            // uint32_t indirect_buf[EXT2_BLOCK_SIZE / 4];
            uint32_t *indirect_buf = kmalloc(EXT2_BLOCK_SIZE);

            ext2_read_block(stream->inode.i_block[12], (uint8_t*)indirect_buf);
            block_num = indirect_buf[indirect_index];
            
            // kprintf("[EXT2] raw bytes: %02x %02x %02x %02x\n",
            //             ((uint8_t*)indirect_buf)[0], ((uint8_t*)indirect_buf)[1],
            //             ((uint8_t*)indirect_buf)[2], ((uint8_t*)indirect_buf)[3]);
            // kprintf("[EXT2] as uint32: %d\n", indirect_buf[0]);
            kfree(indirect_buf);
        }
        else {
            // Indirección doble
            uint32_t double_index = block_index - 12 - 256;
            uint32_t l1_index = double_index / 256;
            uint32_t l2_index = double_index % 256;

            // uint32_t l1_buf[EXT2_BLOCK_SIZE / 4];
            uint32_t *l1_buf = kmalloc(EXT2_BLOCK_SIZE);

            ext2_read_block(stream->inode.i_block[13], (uint8_t*)l1_buf);

            uint32_t *l2_buf = kmalloc(EXT2_BLOCK_SIZE);
            ext2_read_block(l1_buf[l1_index], (uint8_t*)l2_buf);

            block_num = l2_buf[l2_index];
            kfree(l1_buf);
            kfree(l2_buf);
        }

        to_copy = EXT2_BLOCK_SIZE - block_offset;
        if (to_copy > size - bytes_read)
            to_copy = size - bytes_read;

        if (block_num == 0)
        {
            /* Sparse hole: ext2 defines unallocated blocks within a file's
             * range as implicitly zero-filled, not as end-of-file. */
            memset(out + bytes_read, 0, to_copy);
        }
        else
        {
            ext2_read_block(block_num, blockbuf);
            memcpy(out + bytes_read, blockbuf + block_offset, to_copy);
        }
        bytes_read += to_copy;
    }

    kfree(blockbuf);
    stream->pos += bytes_read;
    return bytes_read;
}

static uint32_t ext2_get_or_alloc_block(struct ext2_inode *inode, uint32_t block_index)
{
    uint32_t indirect_index;
    uint32_t* indirect_buf;
    uint32_t block_num;
    uint32_t b;
    uint32_t double_index;
    uint32_t l1_index;
    uint32_t l2_index;
    uint32_t* l1_buf;
    uint32_t* l2_buf;

    if (block_index < 12)
    {
        if (inode->i_block[block_index] == 0)
        {
            b = ext2_allocate_block();
            if (!b)
                return 0;
            inode->i_block[block_index] = b;
            inode->i_blocks += EXT2_BLOCK_SIZE / 512;
        }
        return inode->i_block[block_index];
    }

    if (block_index < 12 + 256)
    {
        indirect_index = block_index - 12;
        indirect_buf = kmalloc(EXT2_BLOCK_SIZE);

        if (inode->i_block[12] == 0)
        {
            b = ext2_allocate_block();
            if (!b)
            {
                kfree(indirect_buf);
                return 0;
            }
            inode->i_block[12] = b;
            inode->i_blocks += EXT2_BLOCK_SIZE / 512;
            memset(indirect_buf, 0, EXT2_BLOCK_SIZE);
            ext2_write_block(inode->i_block[12], indirect_buf);
        }
        else
        {
            ext2_read_block(inode->i_block[12], (uint8_t*)indirect_buf);
        }

        block_num = indirect_buf[indirect_index];
        if (block_num == 0)
        {
            block_num = ext2_allocate_block();
            if (!block_num) { kfree(indirect_buf); return 0; }
            indirect_buf[indirect_index] = block_num;
            inode->i_blocks += EXT2_BLOCK_SIZE / 512;
            ext2_write_block(inode->i_block[12], indirect_buf);
        }
        kfree(indirect_buf);
        return block_num;
    }

    {
        double_index = block_index - 12 - 256;
        l1_index = double_index / 256;
        l2_index = double_index % 256;
        l1_buf = kmalloc(EXT2_BLOCK_SIZE);

        if (inode->i_block[13] == 0)
        {
            b = ext2_allocate_block();
            if (!b) { kfree(l1_buf); return 0; }
            inode->i_block[13] = b;
            inode->i_blocks += EXT2_BLOCK_SIZE / 512;
            memset(l1_buf, 0, EXT2_BLOCK_SIZE);
            ext2_write_block(inode->i_block[13], l1_buf);
        }
        else
        {
            ext2_read_block(inode->i_block[13], (uint8_t*)l1_buf);
        }

        l2_buf = kmalloc(EXT2_BLOCK_SIZE);
        if (l1_buf[l1_index] == 0)
        {
            b = ext2_allocate_block();
            if (!b) { kfree(l1_buf); kfree(l2_buf); return 0; }
            l1_buf[l1_index] = b;
            inode->i_blocks += EXT2_BLOCK_SIZE / 512;
            ext2_write_block(inode->i_block[13], l1_buf);
            memset(l2_buf, 0, EXT2_BLOCK_SIZE);
            ext2_write_block(l1_buf[l1_index], l2_buf);
        }
        else
        {
            ext2_read_block(l1_buf[l1_index], (uint8_t*)l2_buf);
        }

        block_num = l2_buf[l2_index];
        if (block_num == 0)
        {
            block_num = ext2_allocate_block();
            if (!block_num) { kfree(l1_buf); kfree(l2_buf); return 0; }
            l2_buf[l2_index] = block_num;
            inode->i_blocks += EXT2_BLOCK_SIZE / 512;
            ext2_write_block(l1_buf[l1_index], l2_buf);
        }
        kfree(l1_buf);
        kfree(l2_buf);
        return block_num;
    }
}

size_t ext2_fwrite(ext2_FILE *stream, const void *ptr, size_t size)
{
    uint32_t file_offset;
    uint32_t block_index;
    uint32_t block_offset;
    uint32_t block_num;
    uint8_t *blockbuf;
    const uint8_t *in;
    size_t bytes_written;
    size_t to_copy;

    if (!stream)
        return 0;

    if (stream->mode != 1)
    {
        kprintf("ext2_fwrite: file not opened for writing\n");
        return 0;
    }

    if (size == 0) return 0;

    blockbuf = kmalloc(EXT2_BLOCK_SIZE);
    in = (const uint8_t*)ptr;
    bytes_written = 0;

    while (bytes_written < size)
    {
        file_offset = stream->pos + bytes_written;
        block_index = file_offset / EXT2_BLOCK_SIZE;
        block_offset = file_offset % EXT2_BLOCK_SIZE;

        block_num = ext2_get_or_alloc_block(&stream->inode, block_index);
        if (block_num == 0)
        {
            kprintf("ext2_fwrite: no free block\n");
            break;
        }

        to_copy = EXT2_BLOCK_SIZE - block_offset;
        if (to_copy > size - bytes_written)
            to_copy = size - bytes_written;

        if (to_copy == EXT2_BLOCK_SIZE)
        {
            memcpy(blockbuf, in + bytes_written, to_copy);
        }
        else
        {
            /* Partial block: must preserve whatever we're not overwriting. */
            ext2_read_block(block_num, blockbuf);
            memcpy(blockbuf + block_offset, in + bytes_written, to_copy);
        }
        ext2_write_block(block_num, blockbuf);

        bytes_written += to_copy;
    }

    kfree(blockbuf);

    stream->pos += bytes_written;
    if (stream->pos > stream->inode.i_size)
    {
        stream->inode.i_size = stream->pos;
    }
    ext2_write_inode(stream->inode_num, &stream->inode);

    return bytes_written; /* number of bytes actually written */
}

int create_device_node(const char *dir, const char *name, module_t *module)
{
    char path[256];

    strcpy(path, dir);
    strcat(path, "/");
    strcat(path, name);

    uint32_t parent_inode;
    if (ext2_resolve_path(dir, &parent_inode) < 0)
    {
        kprintf("ext2_fopen: parent directory not found '%s'\n", dir);
        return -1;
    }

    int inode_num = ext2_create_file(parent_inode, name, DEVICE_MODE);
    if (inode_num == -1)
    {
        kprintf("create_device_node: failed to create file %s\n", path);
        return -1;
    }

    ext2_FILE *file = ext2_fopen(path, "w");
    if (!file)
    {
        kprintf("create_device_node: failed to open file %s\n", path);
        return -1;
    }
    ext2_fwrite(file, &module->module_id, sizeof(module->module_id));
    ext2_fclose(file);
    return 0;
}

int delete_device_node(const char *dir, const char *name)
{
    char path[256];
    strcpy(path, dir);
    strcat(path, "/");
    strcat(path, name);

    uint32_t inode_num;
    if (ext2_resolve_path(path, &inode_num) < 0)
    {
        kprintf("delete_device_node: file not found '%s'\n", path);
        return -1;
    }

    // ext2_remove_dir_entry(EXT2_ROOT_INODE, name);
    ext2_cmd_rm(path);
    // ext2_free_inode(inode_num);
    return 0;
}

static ssize_t proc_file_read(void *fp, void *buf, size_t count)
{
    proc_buf_t *pb = (proc_buf_t*)fp;
    size_t remain = pb->len - pb->pos;
    size_t n = (count < remain) ? count : remain;
    if (n > 0)
        memcpy(buf, pb->data + pb->pos, n);
    pb->pos += n;
    return n;
}

static int proc_file_close(void *fp)
{
    proc_buf_t *pb = (proc_buf_t*)fp;
    if (pb)
    {
        kfree(pb->data);
        kfree(pb);
    }
    return 0;
}

static char proc_state_char(task_state_t state)
{
    switch (state)
    {
        case TASK_RUNNING:
        case TASK_READY:    return 'R';
        case TASK_ZOMBIE:   return 'Z';
        case TASK_WAITING:
        case TASK_SLEEPING: return 'S';
        default:            return 'S';
    }
}

static task_t *proc_lookup(const char *path, const char **rest)
{
    const char *p;
    uint32_t pid = 0;

    if (strncmp(path, "/proc/", 6) != 0)
        return NULL;
    p = path + 6;
    if (*p < '0' || *p > '9')
        return NULL;
    while (*p >= '0' && *p <= '9')
    {
        pid = pid * 10 + (uint32_t)(*p - '0');
        p++;
    }
    if (*p == '/')
        p++;
    if (rest)
        *rest = p;

    return get_task_by_pid((pid_t)pid);
}

static void proc_append_uint(char *buf, uint32_t val)
{
    char tmp[12];
    uitoa(val, tmp, 10);
    strcat(buf, tmp);
    strcat(buf, " ");
}

static proc_buf_t *proc_build_stat(task_t *t)
{
    proc_buf_t *pb = kmalloc(sizeof(proc_buf_t));
    char *buf = kmalloc(256);
    char tmp[4];
    uint32_t ppid = t->parent ? t->parent->pid : 0;

    buf[0] = '\0';
    uitoa(t->pid, tmp, 10); /* reused below just for the pid itself */
    strcat(buf, tmp);
    strcat(buf, " (");
    strcat(buf, t->name);
    strcat(buf, ") ");
    tmp[0] = proc_state_char(t->state);
    tmp[1] = '\0';
    strcat(buf, tmp);
    strcat(buf, " ");
    proc_append_uint(buf, ppid); /* ppid */
    proc_append_uint(buf, t->pid); /* pgid */
    proc_append_uint(buf, t->pid); /* sid */
    strcat(buf, "-1 0 "); /* tty, tpgid */
    strcat(buf, "0 0 0 0 0 "); /* flags, min_flt, cmin_flt, maj_flt, cmaj_flt */
    strcat(buf, "0 0 "); /* utime, stime */
    strcat(buf, "0 0 0 "); /* cutime, cstime, priority */
    strcat(buf, "0 "); /* nice */
    strcat(buf, "0 0 "); /* timeout, it_real_value */
    strcat(buf, "0 "); /* start_time */
    strcat(buf, "0 "); /* vsize */
    strcat(buf, "0"); /* rss */

    pb->data = buf;
    pb->len = strlen(buf);
    pb->pos = 0;
    return pb;
}

static bool proc_open_file(task_t *current, int fd, const char *subpath, task_t *target)
{
    proc_buf_t *pb;

    if (strcmp(subpath, "stat") != 0)
        return false;

    pb = proc_build_stat(target);

    memset(&current->fd_pointers[fd], 0, sizeof(file_t));
    current->fd_pointers[fd].type = FD_FILE;
    current->fd_pointers[fd].fp = pb;
    current->fd_pointers[fd].fops.read = proc_file_read;
    current->fd_pointers[fd].fops.close = proc_file_close;
    current->fd_pointers[fd].ref_count = 1;
    current->fd_table[fd] = true;
    return true;
}

/* --- Fileio fds Implementations --- */
int sys_open(const char *path, int flags)
{
    task_t *current = get_current_task();
    int fd;
    char mode_str[4];
    ext2_FILE *fp;

    /* Find a free file descriptor slot */
    for (fd = 0; fd < MAX_FDS; fd++)
    {
        if (current->fd_table[fd] == false)
            break;
    }
    if (fd == MAX_FDS)
    {
        kprintf("sys_open: too many open files\n");
        return -1;
    }

    if (strcmp(path, "/proc/mounts") == 0 || strcmp(path, "/proc/self/mounts") == 0)
        path = "/etc/mtab";

    if (strcmp(path, "/proc") == 0 || strcmp(path, "/proc/") == 0)
    {
        memset(&current->fd_pointers[fd], 0, sizeof(file_t));
        current->fd_pointers[fd].type = FD_PROC_DIR;
        current->fd_pointers[fd].offset = 0; /* index into the live pid range */
        current->fd_pointers[fd].flags = flags;
        current->fd_pointers[fd].ref_count = 1;
        current->fd_table[fd] = true;
        return fd;
    }

    if (strncmp(path, "/proc/", 6) == 0)
    {
        const char *subpath;
        task_t *target = proc_lookup(path, &subpath);
        if (!target)
            return -2; // -ENOENT
        if (*subpath == '\0')
            return -21; // -EISDIR: "/proc/<pid>" itself isn't a readable file
        if (proc_open_file(current, fd, subpath, target))
            return fd;
        return -2; // -ENOENT: no such synthesized file under this pid
    }

    {
        /* handle directory case */
        uint32_t dir_inode_num;
        if (ext2_resolve_path(path, &dir_inode_num) == 0)
        {
            struct ext2_inode dir_inode;
            ext2_read_inode(dir_inode_num, &dir_inode);
            if (dir_inode.i_mode & DIR_MODE)
            {
                memset(&current->fd_pointers[fd], 0, sizeof(file_t));
                current->fd_pointers[fd].type = FD_DIR;
                current->fd_pointers[fd].fp = (void*)(uintptr_t)dir_inode_num;
                current->fd_pointers[fd].offset = 0;
                current->fd_pointers[fd].flags = flags;
                current->fd_pointers[fd].ref_count = 1;
                current->fd_table[fd] = true;
                return fd;
            }
        }
    }

    /* For simplicity, if O_WRONLY or O_RDWR with O_CREAT is specified,
       choose an appropriate mode string */
    if (flags & O_CREAT)
    {
        if (flags & O_TRUNC)
            strcpy(mode_str, "w");
        else if (flags & O_APPEND)
            strcpy(mode_str, "a");
        else
            strcpy(mode_str, "r+");
    }
    else
    {
        strcpy(mode_str, "r");
    }

    fp = ext2_fopen(path, mode_str);
    if (!fp)
        return -1;

    /* Fill the file_t structure in the task's fd_pointers array */
    if (fp->inode.i_mode & DEVICE_MODE)
    {
        /*fill module fileops*/
        int mod_num;
        ext2_fread(fp, &mod_num, 4);

        module_t *mod = get_module_by_id(mod_num);
        if (!mod)
        {
            kprintf("sys_open: module not found: %d\n", mod_num);
            return -1;
        }
        current->fd_pointers[fd].fops.read = (void*)mod->read;
        current->fd_pointers[fd].fops.write = NULL;
        current->fd_pointers[fd].fops.close = NULL;
        current->fd_pointers[fd].fp = mod;
        current->fd_pointers[fd].type = FD_MODULE;
        current->fd_pointers[fd].offset = 0;
        ext2_fclose(fp); /* Not needing file anymore. */
    }
    else
    {
        /* TODO REVIEWWWW!!! */
        current->fd_pointers[fd].fops.read = (void*)ext2_fread;
        current->fd_pointers[fd].fops.write = (void*)ext2_fwrite;
        current->fd_pointers[fd].fops.close = (void*)ext2_fclose;
        /* TODO REVIEWWWW!!! */
        current->fd_pointers[fd].type = FD_FILE;
        current->fd_pointers[fd].fp = fp;
        current->fd_pointers[fd].offset = fp->pos;
    }

    // else if (file_obj->type == FD_MODULE)
    // {
    //     int mod_num;
    //     n = ext2_fread(&mod_num, 4, 1, file_obj->file);
    //     /* TODO
    //      * With modnum, simply call the read function of the module
    //      */
    //     module_t *mod = file_obj->module;

    //     if (mod && mod->read)
    //     {
    //         size_t offset = file_obj->offset;
    //         mod->read(mod, buf, count, &offset);
    //         file_obj->offset = offset;
    //         return count;
    //     }
    //     return -1;
    // }


    current->fd_pointers[fd].flags = flags;
    current->fd_pointers[fd].ref_count = 1;
    current->fd_table[fd] = true;

    return fd;
}

int sys_chmod(const char *path, int mode)
{
    uint32_t inode_num;
    struct ext2_inode inode;

    if (ext2_resolve_path(path, &inode_num) < 0)
        return -2; // -ENOENT

    ext2_read_inode(inode_num, &inode);

    inode.i_mode = (inode.i_mode & 0xF000) | (mode & 0x0FFF);
    ext2_write_inode(inode_num, &inode);

    return 0;
}

int sys_chdir(const char *path)
{
    uint32_t inode_num;
    struct ext2_inode inode;

    if (ext2_resolve_path(path, &inode_num) < 0)
        return -2; // -ENOENT

    ext2_read_inode(inode_num, &inode);
    if (!(inode.i_mode & DIR_MODE))
        return -20; // -ENOTDIR

    set_current_dir(inode_num);
    return 0;
}

int sys_statfs64(const char *path, uint32_t bufsize, struct kfs_statfs64 *buf)
{
    uint32_t inode_num;
    (void)bufsize;

    if (!path || !buf)
        return -14; // -EFAULT
    if (ext2_resolve_path(path, &inode_num) < 0)
        return -2; // -ENOENT

    memset(buf, 0, sizeof(*buf));
    buf->f_type    = EXT2_SUPER_MAGIC;
    buf->f_bsize   = EXT2_BLOCK_SIZE;
    buf->f_blocks  = ext2.sb.s_blocks_count;
    buf->f_bfree   = ext2.sb.s_free_blocks_count;
    buf->f_bavail  = (ext2.sb.s_free_blocks_count > ext2.sb.s_r_blocks_count)
                        ? (ext2.sb.s_free_blocks_count - ext2.sb.s_r_blocks_count)
                        : 0;
    buf->f_files   = ext2.sb.s_inodes_count;
    buf->f_ffree   = ext2.sb.s_free_inodes_count;
    buf->f_namelen = 255;
    buf->f_frsize  = EXT2_BLOCK_SIZE;
    return 0;
}

int sys_close(int fd, task_t *task)
{
    // task_t* current;
    file_t* file_obj;
    
    // current = get_current_task();
    if (fd < 0 || fd >= MAX_FDS || task->fd_table[fd] == false)
        return -1;

    /* Get pointer to the file object in the array */
    file_obj = &task->fd_pointers[fd];
    if (file_obj->ref_count > 1)
    {
        file_obj->ref_count--;
        return 0;
    }
    if (file_obj->fops.close)
        file_obj->fops.close(file_obj->fp);
    /* TODO modify */
    // if (file_obj->type == FD_SOCKET)
    // {
    //     socket_close(file_obj->fp);
    // }
    // else
    //     ext2_fclose(file_obj->fp);
    
    /* Mark slot as free and zero out the structure */
    task->fd_table[fd] = false;
    memset(&task->fd_pointers[fd], 0, sizeof(file_t));
    return 0;
}

ssize_t sys_read(int fd, void *buf, size_t count)
{
    task_t *current;
    file_t *file_obj;
    size_t n;

    current = get_current_task();
    if (fd < 0 || fd >= MAX_FDS || current->fd_table[fd] == false)
        return -1;

    file_obj = &current->fd_pointers[fd];

    if (file_obj->type == FD_DIR)
        return -21; // -EISDIR

    if (file_obj->type == FD_MODULE)
    {
        module_t *mod = file_obj->fp;
        if (mod && mod->read)
        {
            size_t offset = file_obj->offset;
            mod->read(buf, count, (size_t*)&file_obj->offset);
            if (file_obj->offset > 0)
                return file_obj->offset - offset;
            else
                return file_obj->offset;
        }
        return -1;
    }

    if (file_obj->fops.read)
        return file_obj->fops.read(file_obj->fp, buf, count);
    else
        return -1;
    /* TODO fix file and module read. */


    // if (file_obj->type == FD_SOCKET)
    // {
    //     return socket_recv(file_obj->socket, buf, count);
    // }
    // else if (file_obj->type == FD_MODULE)
    // {
    //     int mod_num;
    //     n = ext2_fread(&mod_num, 4, 1, file_obj->file);
    //     /* TODO
    //      * With modnum, simply call the read function of the module
    //      */
    //     module_t *mod = file_obj->module;

    //     if (mod && mod->read)
    //     {
    //         size_t offset = file_obj->offset;
    //         mod->read(mod, buf, count, &offset);
    //         file_obj->offset = offset;
    //         return count;
    //     }
    //     return -1;
    // }

    // n = ext2_fread(buf, 1, count, file_obj->file);
    // file_obj->offset = file_obj->file->pos;
    return n;
}

struct linux_dirent64
{
    uint64_t d_ino;
    int64_t  d_off;
    uint16_t d_reclen;
    uint8_t  d_type;
    char     d_name[];
};

#define DT_UNKNOWN 0
#define DT_REG     8
#define DT_DIR     4

static uint8_t ext2_file_type_to_dt(uint8_t ext2_file_type)
{
    switch (ext2_file_type)
    {
        case EXT2_FT_REG_FILE: return DT_REG;
        case EXT2_FT_DIR:      return DT_DIR;
        default:                return DT_UNKNOWN;
    }
}

ssize_t sys_getdents64(int fd, void *dirp, size_t count)
{
    task_t *current = get_current_task();
    file_t *file_obj;
    uint8_t *blkbuf;
    uint8_t *out;
    uint8_t *out2;
    size_t written;
    size_t written2;
    uint32_t offset;
    uint32_t inode_num;
    pid_t p;
    pid_t max_pid;
    struct ext2_inode dir;
    task_t *t;
    char namebuf[12];
    size_t namelen;
    size_t reclen;
    struct linux_dirent64 *out_de;



    if (fd < 0 || fd >= MAX_FDS || current->fd_table[fd] == false)
        return -1;

    file_obj = &current->fd_pointers[fd];

    if (file_obj->type == FD_PROC_DIR)
    {
        out2 = (uint8_t*)dirp;
        written2 = 0;
        p = (pid_t)file_obj->offset;
        max_pid = get_max_pid();

        for (; p <= max_pid; p++)
        {
            t = get_task_by_pid(p);
            if (!t)
                continue;

            uitoa((uint32_t)p, namebuf, 10);
            namelen = strlen(namebuf);
            reclen = ((size_t)&((struct linux_dirent64*)0)->d_name + namelen + 1 + 7) & ~7u;

            if (written2 + reclen > count)
                break; /* buffer full; resume at this pid next call */

            out_de = (struct linux_dirent64 *)(out2 + written2);
            out_de->d_ino = (uint64_t)p;
            out_de->d_reclen = reclen;
            out_de->d_type = DT_DIR;
            memcpy(out_de->d_name, namebuf, namelen);
            out_de->d_name[namelen] = '\0';
            out_de->d_off = p + 1;

            written2 += reclen;
        }

        file_obj->offset = (uint32_t)p;
        return written2;
    }

    if (file_obj->type != FD_DIR)
        return -20; // -ENOTDIR

    inode_num = (uint32_t)(uintptr_t)file_obj->fp;
    ext2_read_inode(inode_num, &dir);

    /* Directories in this driver live entirely in their first data block
     * (see ext2_cmd_ls) — matches the rest of the codebase's assumptions. */
    blkbuf = kmalloc(EXT2_BLOCK_SIZE);
    if (dir.i_block[0])
        ext2_read_block(dir.i_block[0], blkbuf);
    else
        memset(blkbuf, 0, EXT2_BLOCK_SIZE);

    out = (uint8_t*)dirp;
    written = 0;
    offset = file_obj->offset;

    while (offset < EXT2_BLOCK_SIZE)
    {
        struct ext2_dir_entry *de = (struct ext2_dir_entry *)(blkbuf + offset);
        if (de->rec_len == 0)
            break;

        if (de->inode != 0)
        {
            size_t namelen = de->name_len;
            size_t reclen = ((size_t)&((struct linux_dirent64*)0)->d_name + namelen + 1 + 7) & ~7u;

            if (written + reclen > count)
                break; /* caller's buffer is full; resume here next call */

            struct linux_dirent64 *out_de = (struct linux_dirent64 *)(out + written);
            out_de->d_ino = de->inode;
            out_de->d_reclen = reclen;
            out_de->d_type = ext2_file_type_to_dt(de->file_type);
            memcpy(out_de->d_name, de->name, namelen);
            out_de->d_name[namelen] = '\0';

            offset += de->rec_len;
            out_de->d_off = offset;

            written += reclen;
        }
        else
        {
            offset += de->rec_len;
        }
    }

    file_obj->offset = offset;
    kfree(blkbuf);
    return written;
}

ssize_t sys_lseek(int fd, ssize_t offset, int whence)
{
    task_t *current = get_current_task();
    if (fd < 0 || fd >= MAX_FDS || !current->fd_table[fd])
        return -EBADF;

    file_t *file_obj = &current->fd_pointers[fd];
    if (file_obj->type != FD_FILE)
        return -1;

    ext2_FILE *fp = file_obj->fp;
    ssize_t new_pos;
    if (whence == SEEK_SET)
        new_pos = offset;
    else if (whence == SEEK_CUR)
        new_pos = (ssize_t)fp->pos + offset;
    else if (whence == SEEK_END)
        new_pos = (ssize_t)fp->inode.i_size + offset;
    else
        return -1;

    if (new_pos < 0 || (size_t)new_pos > fp->inode.i_size)
        return -1;

    fp->pos = new_pos;
    file_obj->offset = new_pos;
    return new_pos;
}

/* It's supposed to work but has not been tested. */
int dup(int oldfd)
{
    task_t *current = get_current_task();
    if (oldfd < 0 || oldfd >= MAX_FDS || !current->fd_table[oldfd])
        return -EBADF;

    int newfd;
    for (newfd = 0; newfd < MAX_FDS; newfd++)
    {
        if (!current->fd_table[newfd])
            break;
    }
    if (newfd == MAX_FDS)
        return -EMFILE;

    current->fd_table[newfd] = current->fd_table[oldfd];
    current->fd_pointers[newfd] = current->fd_pointers[oldfd];
    current->fd_pointers[newfd].ref_count++;
    return newfd;
}


ssize_t sys_write(int fd, const void *buf, size_t count)
{
    task_t *current;
    file_t *file_obj;
    size_t n;

    // if (fd == 1)
    // {
    //     puts(buf);
    //     return count;
    // }

    current = get_current_task();
    if (fd < 0 || fd >= MAX_FDS || current->fd_table[fd] == false)
        return -1;

    file_obj = &current->fd_pointers[fd];
    if (file_obj->fops.write)
        n = file_obj->fops.write(file_obj->fp, buf, count);

    if (file_obj->type == FD_FILE)
    {
        file_obj->offset = ((ext2_FILE*)file_obj->fp)->pos;
    }
    
    // if (file_obj->type == FD_SOCKET)
    // {
    //     return socket_send(file_obj->fp, buf, count);
    // }

    /* TODO move this to fwrite. */
    // n = ext2_fwrite(file_obj->fp, buf, count);
    // file_obj->offset = file_obj->fp->pos;
    return n;
}

/* --- Command Implementations --- */
void ext2_cmd_ls(const char *path)
{
    uint32_t inode_num;
    if (ext2_resolve_path(path, &inode_num) < 0)
    {
        kprintf("Directory not found\n");
        return;
    }
    struct ext2_inode dir;
    ext2_read_inode(inode_num, &dir);
    if (!(dir.i_mode & DIR_MODE))
    {
        kprintf("Not a directory\n");
        return;
    }
    uint8_t* blkbuf = kmalloc(EXT2_BLOCK_SIZE);
    ext2_read_block(dir.i_block[0], blkbuf);
    int offset = 0;
    struct ext2_dir_entry *de;
    while (offset < EXT2_BLOCK_SIZE)
    {
        de = (struct ext2_dir_entry *)(blkbuf + offset);
        if (de->inode != 0)
        {
            char name[256];
            memcpy(name, de->name, de->name_len);
            name[de->name_len] = '\0';
            kprintf("%s  ", name);
        }
        offset += de->rec_len;
    }
    kprintf("\n");
    kfree(blkbuf);
}

void ext2_cmd_cat(const char *path)
{
    uint32_t inode_num;
    if (ext2_resolve_path(path, &inode_num) < 0)
    {
        kprintf("File not found\n");
        return;
    }
    struct ext2_inode file;
    ext2_read_inode(inode_num, &file);
    if (file.i_mode & DIR_MODE)
    {
        kprintf("Is a directory\n");
        return;
    }
    if (file.i_block[0] == 0)
        return;
    uint8_t* blkbuf = kmalloc(EXT2_BLOCK_SIZE);
    ext2_read_block(file.i_block[0], blkbuf);
    int size = file.i_size < EXT2_BLOCK_SIZE ? file.i_size : EXT2_BLOCK_SIZE;
    for (int i = 0; i < size; i++)
        putc(blkbuf[i]);
    kfree(blkbuf);
}

void ext2_cmd_touch(const char *path)
{
    uint32_t inode_num;
    if (ext2_resolve_path(path, &inode_num) == 0)
    {
        struct ext2_inode file;
        ext2_read_inode(inode_num, &file);
        file.i_ctime = 0; /* TODO update with current time */
        ext2_write_inode(inode_num, &file);
        return;
    }
    char parent_path[256], file_name[256];
    strcpy(parent_path, path);
    char *last_slash = strrchr(parent_path, '/');
    if (last_slash)
    {
        strcpy(file_name, last_slash+1);
        if (last_slash == parent_path)
            strcpy(parent_path, "/");
        else
            *last_slash = '\0';
    }
    else
    {
        strcpy(file_name, path);
        strcpy(parent_path, ".");
    }
    uint32_t parent;
    if (ext2_resolve_path(parent_path, &parent) < 0)
    {
        kprintf("Parent directory not found\n");
        return;
    }
    ext2_create_file(parent, file_name, FILE_MODE);
}

void ext2_cmd_mkdir(const char *path)
{
    char parent_path[256];
    char dir_name[256];
    char *last_slash;
    uint32_t parent;

    /* check if it exists. */
    if (ext2_resolve_path(path, &parent) == 0)
    {
        kprintf("Directory '%s' already exists\n", path);
        return;
    }

    strcpy(parent_path, path);
    last_slash = strrchr(parent_path, '/');
    
    if (last_slash)
    {
        strcpy(dir_name, last_slash+1);
        if (last_slash == parent_path)
            strcpy(parent_path, "/");
        else
            *last_slash = '\0';
    }
    else
    {
        strcpy(dir_name, path);
        strcpy(parent_path, ".");
    }
    if (ext2_resolve_path(parent_path, &parent) < 0)
    {
        kprintf("Parent directory not found\n");
        return;
    }
    uint32_t new_inode = ext2_allocate_inode();
    if (new_inode == 0)
    {
        kprintf("No free inode available\n");
        return;
    }
    struct ext2_inode dir;
    memset(&dir, 0, sizeof(dir));
    dir.i_mode = DIR_MODE;  /* directory */
    dir.i_size = EXT2_BLOCK_SIZE;
    dir.i_links_count = 2; /* . and .. */
    uint32_t block = ext2_allocate_block();
    if (block == 0)
    {
        kprintf("No free block available\n");
        return;
    }
    dir.i_block[0] = block;
    dir.i_blocks = 2;
    ext2_write_inode(new_inode, &dir);
    /* Initialize new directory block with '.' and '..' */
    uint8_t* blkbuf = kmalloc(EXT2_BLOCK_SIZE);
    memset(blkbuf, 0, EXT2_BLOCK_SIZE);
    struct ext2_dir_entry *de = (struct ext2_dir_entry *)blkbuf;
    de->inode = new_inode;
    de->rec_len = 12;
    de->name_len = 1;
    de->file_type = EXT2_FT_DIR;
    strcpy(de->name, ".");
    struct ext2_dir_entry *de2 = (struct ext2_dir_entry *)(blkbuf + 12);
    de2->inode = parent;
    de2->rec_len = EXT2_BLOCK_SIZE - 12;
    de2->name_len = 2;
    de2->file_type = EXT2_FT_DIR;
    strcpy(de2->name, "..");
    ext2_write_block(block, blkbuf);
    kfree(blkbuf);
    ext2_add_dir_entry(parent, dir_name, new_inode, EXT2_FT_DIR);
}

void ext2_cmd_rm(const char *path)
{
    uint32_t inode_num;
    if (ext2_resolve_path(path, &inode_num) < 0)
    {
        kprintf("File not found\n");
        return;
    }
    struct ext2_inode file;
    ext2_read_inode(inode_num, &file);
    if (file.i_mode & DIR_MODE)
    {
        kprintf("Is a directory – use rmdir\n");
        return;
    }
    char parent_path[256], file_name[256];
    strcpy(parent_path, path);
    char *last_slash = strrchr(parent_path, '/');
    if (last_slash)
    {
        strcpy(file_name, last_slash+1);
        if (last_slash == parent_path)
            strcpy(parent_path, "/");
        else
            *last_slash = '\0';
    }
    else
    {
        strcpy(file_name, path);
        strcpy(parent_path, ".");
    }
    uint32_t parent;
    if (ext2_resolve_path(parent_path, &parent) < 0)
    {
        kprintf("Parent directory not found\n");
        return;
    }
    ext2_remove_dir_entry(parent, file_name);
    if (file.i_block[0])
        ext2_free_block(file.i_block[0]);
    ext2_free_inode(inode_num);
}

void ext2_cmd_rmdir(const char *path)
{
    uint32_t inode_num;
    if (ext2_resolve_path(path, &inode_num) < 0)
    {
        kprintf("Directory not found\n");
        return;
    }
    struct ext2_inode dir;
    ext2_read_inode(inode_num, &dir);
    if (!(dir.i_mode & DIR_MODE))
    {
        kprintf("Not a directory\n");
        return;
    }
    uint8_t* blkbuf = kmalloc(EXT2_BLOCK_SIZE);
    ext2_read_block(dir.i_block[0], blkbuf);
    int offset = 0, count = 0;
    struct ext2_dir_entry *de;
    while (offset < EXT2_BLOCK_SIZE)
    {
        de = (struct ext2_dir_entry *)(blkbuf + offset);
        if (de->inode != 0)
            count++;
        offset += de->rec_len;
    }
    kfree(blkbuf);
    if (count > 2)
    {
        kprintf("Directory not empty\n");
        return;
    }
    char parent_path[256], dname[256];
    strcpy(parent_path, path);
    char *last_slash = strrchr(parent_path, '/');
    if (last_slash)
    {
        strcpy(dname, last_slash+1);
        if (last_slash == parent_path)
            strcpy(parent_path, "/");
        else
            *last_slash = '\0';
    }
    else
    {
        strcpy(dname, path);
        strcpy(parent_path, ".");
    }
    uint32_t parent;
    if (ext2_resolve_path(parent_path, &parent) < 0)
    {
        kprintf("Parent directory not found\n");
        return;
    }
    ext2_remove_dir_entry(parent, dname);
    ext2_free_block(dir.i_block[0]);
    ext2_free_inode(inode_num);
}

void ext2_cmd_cd(const char *path)
{
    uint32_t inode_num;
    if (ext2_resolve_path(path, &inode_num) < 0)
    {
        kprintf("Directory not found\n");
        return;
    }
    struct ext2_inode inode;
    ext2_read_inode(inode_num, &inode);
    if (!(inode.i_mode & DIR_MODE))
    {
        kprintf("Not a directory\n");
        return;
    }
    current_dir = inode_num;
}

void ext2_cmd_cp(const char *src_path, const char *dst_path)
{
    uint32_t src_inode_num;
    if (ext2_resolve_path(src_path, &src_inode_num) < 0)
    {
        kprintf("cp: source not found: %s\n", src_path);
        return;
    }

    struct ext2_inode src_inode;
    ext2_read_inode(src_inode_num, &src_inode);

    if (src_inode.i_mode & DIR_MODE)
    {
        kprintf("cp: source is a directory (not supported)\n");
        return;
    }

    uint8_t *data_buf = kmalloc(EXT2_BLOCK_SIZE);
    memset(data_buf, 0, EXT2_BLOCK_SIZE);
    int size = 0;
    if (src_inode.i_block[0] != 0)
    {
        ext2_read_block(src_inode.i_block[0], data_buf);
        size = (src_inode.i_size < EXT2_BLOCK_SIZE) ? src_inode.i_size : EXT2_BLOCK_SIZE;
    }

    char parent_path[256], file_name[256];
    split_path(dst_path, parent_path, file_name);

    uint32_t parent_inode_num;
    if (ext2_resolve_path(parent_path, &parent_inode_num) < 0)
    {
        kprintf("cp: destination directory not found: %s\n", parent_path);
        kfree(data_buf);
        return;
    }

    uint32_t new_inode_num = ext2_create_file(parent_inode_num, file_name, FILE_MODE /* regular file */);
    if (new_inode_num == 0)
    {
        kprintf("cp: failed to create destination file: %s\n", dst_path);
        kfree(data_buf);
        return;
    }

    struct ext2_inode new_inode;
    ext2_read_inode(new_inode_num, &new_inode);
    new_inode.i_size = size;
    if (size > 0)
    {
        uint32_t new_block = ext2_allocate_block();
        if (!new_block)
        {
            kprintf("cp: no free block available\n");
            kfree(data_buf);
            return;
        }
        new_inode.i_block[0] = new_block;
        new_inode.i_blocks = 2; // 1 block = 2 in 512-byte units
        ext2_write_block(new_block, data_buf);
    }
    ext2_write_inode(new_inode_num, &new_inode);

    kfree(data_buf);
    kprintf("cp: copied '%s' to '%s'\n", src_path, dst_path);
}

void ext2_cmd_mv(const char *src_path, const char *dst_path)
{
    uint32_t src_inode_num;
    if (ext2_resolve_path(src_path, &src_inode_num) < 0)
    {
        kprintf("mv: source not found: %s\n", src_path);
        return;
    }

    char src_parent[256], src_name[256];
    split_path(src_path, src_parent, src_name);

    char dst_parent[256], dst_name[256];
    split_path(dst_path, dst_parent, dst_name);

    uint32_t dst_parent_inode;
    if (ext2_resolve_path(dst_parent, &dst_parent_inode) < 0)
    {
        kprintf("mv: destination directory not found: %s\n", dst_parent);
        return;
    }

    uint32_t dst_inode_num;
    if (ext2_resolve_path(dst_path, &dst_inode_num) == 0)
    {
        // For simplicity, we fail if destination exists
        // (In real Unix, we'd remove it if it's a file, or fail if it's a non-empty directory)
        kprintf("mv: destination already exists: %s\n", dst_path);
        return;
    }

    struct ext2_inode src_inode;
    ext2_read_inode(src_inode_num, &src_inode);
    uint8_t file_type = (src_inode.i_mode & DIR_MODE) ? EXT2_FT_DIR : EXT2_FT_REG_FILE;
    if (ext2_add_dir_entry(dst_parent_inode, dst_name, src_inode_num, file_type) < 0)
    {
        kprintf("mv: failed to create destination entry\n");
        return;
    }

    uint32_t src_parent_inode;
    if (ext2_resolve_path(src_parent, &src_parent_inode) < 0)
    {
        kprintf("mv: source parent not found?!\n");
        return;
    }
    if (ext2_remove_dir_entry(src_parent_inode, src_name) < 0)
    {
        kprintf("mv: failed to remove old directory entry\n");
        return;
    }

    kprintf("mv: moved '%s' to '%s'\n", src_path, dst_path);
}

static void cmd_ls();
static void cmd_cat();
static void cmd_touch();
static void cmd_mkdir();
static void cmd_rm();
static void cmd_rmdir();
static void cmd_cd();
static void cmd_cp();
static void cmd_mv();
static void cmd_pwd(void);
static void cmd_tfds(void);
static void cmd_tfdopen();

command_t ext2_commands[] = {
    {"ls", "List directory contents", cmd_ls},
    {"cat", "Concatenate files and print on the standard output", cmd_cat},
    {"touch", "Change file timestamps", cmd_touch},
    {"mkdir", "Make directories", cmd_mkdir},
    {"rm", "Remove files or directories", cmd_rm},
    {"rmdir", "Remove directories", cmd_rmdir},
    {"cd", "Change the shell working directory", cmd_cd},
    {"cp", "Copy a file", cmd_cp},
    {"mv", "Move or rename a file", cmd_mv},
    {"pwd", "Print working directory", cmd_pwd},
    {NULL, NULL, NULL}
};

command_t ext2_commands_debug[] = {
    {"tfds", "Test fds implementation", cmd_tfds},
    {"tfdopen", "Test fopen implementation", cmd_tfdopen},
    {NULL, NULL, NULL}
};

static void cmd_ls()
{
    ext2_cmd_ls(".");
}

static void cmd_cat()
{
    kprintf("Enter the file name: ");
    ext2_cmd_cat(get_line());
}

static void cmd_touch()
{
    kprintf("Enter the file name: ");
    ext2_cmd_touch(get_line());
}

static void cmd_mkdir()
{
    kprintf("Enter the directory name: ");
    ext2_cmd_mkdir(get_line());
}

static void cmd_rm()
{
    kprintf("Enter the file name: ");
    ext2_cmd_rm(get_line());
}

static void cmd_rmdir()
{
    kprintf("Enter the directory name: ");
    ext2_cmd_rmdir(get_line());
}

static void cmd_cd()
{
    kprintf("Enter the directory name: ");
    ext2_cmd_cd(get_line());
}
static void cmd_cp()
{
    kprintf("Enter source file: ");
    char *src = get_line();
    kprintf("Enter destination: ");
    char *dst = get_line();
    ext2_cmd_cp(src, dst);
}

static void cmd_mv()
{
    kprintf("Enter source file: ");
    char *src = get_line();
    kprintf("Enter destination: ");
    char *dst = get_line();
    ext2_cmd_mv(src, dst);
}

static void cmd_pwd(void)
{
    char *cwd = ext2_pwd();
    if (cwd)
    {
        kprintf("%s\n", cwd);
        kfree(cwd);
    }
    else
    {
        kprintf("pwd: error retrieving current directory\n");
    }
}

static void cmd_tfds(void)
{
    int fd;

    fd = sys_open("/etc/users.config", O_RDONLY);
    if (fd < 0)
    {
        kprintf("Failed to open /etc/users.config\n");
        return;
    }

    char buf[256];
    ssize_t n = sys_read(fd, buf, sizeof(buf));
    if (n < 0)
    {
        kprintf("Failed to read /etc/users.config\n");
        sys_close(fd, get_current_task());
        return;
    }

    kprintf("Read %d bytes from /etc/users.config:\n", n);
    for (int i = 0; i < n; i++)
        putc(buf[i]);
    sys_close(fd, get_current_task());
}

static void cmd_tfdopen()
{
    int fd;
    ssize_t n;
    const char *msg = "Hello from ext12_fwrite!\n";
    char buf[128];

    fd = sys_open("helloTFD.txt", O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0)
    {
        kprintf("Failed to open hello.txt for writing\n");
        return;
    }

    sys_write(fd, msg, strlen(msg));
    sys_close(fd, get_current_task());

    kprintf("Wrote to hello.txt, fd %d\n", fd);

    fd = sys_open("helloTFD.txt", O_RDONLY);
    if (fd < 0)
    {
        kprintf("Failed to open hello.txt for reading\n");
        return;
    }
    memset(buf, 0, sizeof(buf));
    n = sys_read(fd, buf, sizeof(buf) - 1);

    kprintf("Read from fd(%d) '%d' bytes: %s", fd, n, buf);
    
    if (strcmp(buf, msg) != 0)
    {
        kprintf("Failed to read back what was written\n");
        sys_close(fd, get_current_task());
        return;
    }
    kprintf("Read %u bytes: %s\n", (unsigned)n, buf);
    sys_close(fd, get_current_task());
}

static void ext2_write_file_if_missing(const char *path, const char *content)
{
    uint32_t inode_num;
    ext2_FILE *f;

    if (ext2_resolve_path(path, &inode_num) == 0)
        return; /* already present */

    f = ext2_fopen(path, "w");
    if (!f)
    {
        kprintf("Failed to create %s\n", path);
        return;
    }
    ext2_fwrite(f, content, strlen(content));
    ext2_fclose(f);
}

void create_unix_dirs()
{
    ext2_cmd_mkdir("bin");
    ext2_cmd_mkdir("boot");
    ext2_cmd_mkdir("dev");
    ext2_cmd_mkdir("etc");
    ext2_cmd_mkdir("home");
    ext2_cmd_mkdir("lib");
    ext2_cmd_mkdir("mnt");
    ext2_cmd_mkdir("opt");
    ext2_cmd_mkdir("proc");
    ext2_cmd_mkdir("root");
    ext2_cmd_mkdir("run");
    ext2_cmd_mkdir("etc");
    ext2_cmd_mv("users.config", "/etc/users.config");

    ext2_write_file_if_missing("/etc/passwd",
        "root:x:0:0:root:/root:/bin/sh\n"
        "user:x:1000:1000:user:/home:/bin/sh\n");
    ext2_write_file_if_missing("/etc/group",
        "root:x:0:\n"
        "user:x:1000:\n");
    ext2_write_file_if_missing("/etc/mtab",
        "/dev/root / ext2 rw 0 0\n");
}

void test_fileio()
{
    ext2_FILE *f = ext2_fopen("hello.txt", "w");
    if (!f)
    {
        kprintf("Failed to open hello.txt for writing\n");
        return;
    }
    const char *msg = "Hello from ext12_fwrite!\n";
    ext2_fwrite(f, msg, strlen(msg));
    ext2_fclose(f);

    f = ext2_fopen("hello.txt", "r");
    if (!f)
    {
        kprintf("Failed to open hello.txt for reading\n");
        return;
    }
    char buf[128];
    memset(buf, 0, sizeof(buf));
    size_t n = ext2_fread(f, buf, sizeof(buf) - 1);
    kprintf("Read %u bytes: %s\n", (unsigned)n, buf);
    ext2_fclose(f);
}

void ext2_mount(void)
{
    uint8_t* buf = kmalloc(EXT2_BLOCK_SIZE);
    /* Read superblock (located at block 1) */
    ext2_read_block(1, buf);
    memcpy(&ext2.sb, buf, sizeof(struct ext2_super_block));
    if (ext2.sb.s_magic != 0xEF53)
        kpanic("ext2: bad magic number", 1);
    /* Read group descriptor (assumed to be in block 2) */
    ext2_read_block(2, buf);
    memcpy(&ext2.gd, buf, sizeof(struct ext2_group_desc));
    /* Load inode and block bitmaps */
    ext2.inode_bitmap = kmalloc(EXT2_BLOCK_SIZE);
    ext2.block_bitmap = kmalloc(EXT2_BLOCK_SIZE);
    ext2_read_block(ext2.gd.bg_inode_bitmap, ext2.inode_bitmap);
    ext2_read_block(ext2.gd.bg_block_bitmap, ext2.block_bitmap);
    kfree(buf);
    install_all_cmds(ext2_commands, GLOBAL);
    install_all_cmds(ext2_commands_debug, DEBUG);
    create_unix_dirs();
    test_fileio();
}

#define ENOENT 2

struct statx_timestamp {
    int64_t  tv_sec;
    uint32_t tv_nsec;
    int32_t  __reserved;
};

struct statx {
    uint32_t stx_mask;
    uint32_t stx_blksize;
    uint64_t stx_attributes;
    uint32_t stx_nlink;
    uint32_t stx_uid;
    uint32_t stx_gid;
    uint16_t stx_mode;
    uint16_t __spare0[1];
    uint64_t stx_ino;
    uint64_t stx_size;
    uint64_t stx_blocks;
    uint64_t stx_attributes_mask;
    struct statx_timestamp stx_atime;
    struct statx_timestamp stx_btime;
    struct statx_timestamp stx_ctime;
    struct statx_timestamp stx_mtime;
    uint32_t stx_rdev_major;
    uint32_t stx_rdev_minor;
    uint32_t stx_dev_major;
    uint32_t stx_dev_minor;
};

int sys_statx(int dirfd, const char *path, int flags,
              unsigned int mask, void *statxbuf)
{
    (void)dirfd; (void)flags;
    if (!path || !statxbuf)
        return -1;

    const char *resolved = path;
    if (path[0] == '\0') {
        return -1;
    }

    if (strncmp(path, "/proc/", 6) == 0)
    {
        const char *subpath;
        task_t *target = proc_lookup(path, &subpath);
        struct statx *sxp;

        if (!target)
            return -2; // -ENOENT

        sxp = (struct statx*)statxbuf;
        memset(sxp, 0, sizeof(*sxp));
        sxp->stx_mask = mask;
        sxp->stx_mode = DIR_MODE | 0555;
        sxp->stx_nlink = 1;
        sxp->stx_uid = target->uid;
        sxp->stx_gid = target->gid;
        sxp->stx_ino = (uint64_t)target->pid;
        return 0;
    }

    uint32_t inode_num;
    if (ext2_resolve_path(resolved, &inode_num) < 0)
        return -2; // -ENOENT

    struct ext2_inode inode;
    ext2_read_inode(inode_num, &inode);

    struct statx *sx = (struct statx*)statxbuf;
    memset(sx, 0, sizeof(*sx));

    sx->stx_mask     = mask;
    sx->stx_blksize  = 1024;
    sx->stx_nlink    = inode.i_links_count;
    sx->stx_uid      = inode.i_uid;
    sx->stx_gid      = inode.i_gid;
    sx->stx_mode     = inode.i_mode;
    sx->stx_ino      = inode_num;
    sx->stx_size     = inode.i_size;
    sx->stx_blocks   = inode.i_blocks;
    sx->stx_atime.tv_sec = inode.i_atime;
    sx->stx_mtime.tv_sec = inode.i_mtime;
    sx->stx_ctime.tv_sec = inode.i_ctime;
    sx->stx_dev_major = 8;  // sda
    sx->stx_dev_minor = 0;

    return 0;
}

int sys_fadvise64(int fd, off_t offset, off_t len, int advice)
{
    /* no cache, so no-op */
    (void)fd;
    (void)offset;
    (void)len;
    (void)advice;
    return 0;
}
