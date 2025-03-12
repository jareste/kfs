#include "ext2.h"
#include "../ide/ide.h"
#include "../memory/memory.h"
#include "../utils/utils.h"
#include "../utils/stdint.h"
#include "../kshell/kshell.h"
#include "../keyboard/keyboard.h"
#include "../display/display.h"
#include "../tasks/task.h"
#include "ext2_fileio.h"

extern int ide_read_sectors(uint32_t lba, uint8_t count, void *buffer);
extern int ide_write_sectors(uint32_t lba, uint8_t count, void *buffer);

/* --- Derived constants --- */
#define SECTORS_PER_BLOCK (EXT2_BLOCK_SIZE / IDE_SECTOR_SIZE)

/* --- Global FS structure --- */
struct ext2_fs
{
    struct ext2_super_block sb;
    struct ext2_group_desc  gd;
    uint8_t* inode_bitmap;  /* one block */
    uint8_t* block_bitmap;  /* one block */
} ext2;

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
        kernel_panic("ext2: read block error");
}

static void ext2_write_block(uint32_t block, void *buf)
{
    uint32_t lba = EXT2_PARTITION_START + block * SECTORS_PER_BLOCK;
    if (ide_write_sectors(lba, SECTORS_PER_BLOCK, buf) < 0)
        kernel_panic("ext2: write block error");
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

static void ext2_write_inode(uint32_t inode_num, struct ext2_inode *inode) {
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
            printf("No free block available\n");
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
        printf("Directory full, cannot add entry\n");
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
        printf("Not a directory\n");
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

/* used to be static. review */
int ext2_resolve_path(const char *path, uint32_t *inode_out)
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
        printf("ext2_remove_all_files: directory not found: %s\n", dir_path);
        return -1;
    }

    struct ext2_inode dir_inode;
    ext2_read_inode(dir_inode_num, &dir_inode);
    if (!(dir_inode.i_mode & DIR_MODE))
    {
        printf("ext2_remove_all_files: %s is not a directory\n", dir_path);
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
                        printf("ext2_remove_all_files: too many files; increase array size\n");
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


/* used to be static. review */
int ext2_create_file(uint32_t parent_inode_num, const char *name, uint16_t mode)
{
    uint32_t new_inode = ext2_allocate_inode();
    if (new_inode == 0)
    {
        printf("No free inode available\n");
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
        printf("ext2_get_inode: path not found: %s\n", path);
        return 0;
    }
    return inode_num;
}

char *ext2_pwd(void)
{
    if (current_dir == EXT2_ROOT_INODE)
        return vstrdup("/");

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
            uitoa(curr, name);
        }
        components[count++] = vstrdup(name);
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
        printf("ext2_fopen: unsupported mode '%s'\n", mode);
        return NULL;
    }

    uint32_t inode_num;
    if (ext2_resolve_path(path, &inode_num) < 0)
    {
        if (!allow_write)
        {
            printf("ext2_fopen: file not found '%s'\n", path);
            return NULL;
        }
        
        char parent_path[256], file_name[256];
        split_path(path, parent_path, file_name);
        uint32_t parent_inode;
        if (ext2_resolve_path(parent_path, &parent_inode) < 0)
        {
            printf("ext2_fopen: parent directory not found '%s'\n", parent_path);
            return NULL;
        }
        uint32_t new_inode = ext2_create_file(parent_inode, file_name, FILE_MODE);
        if (!new_inode)
        {
            printf("ext2_fopen: cannot create file '%s'\n", path);
            return NULL;
        }
        inode_num = new_inode;
    }

    struct ext2_inode in;
    ext2_read_inode(inode_num, &in);
    if (in.i_mode & DIR_MODE)
    {
        printf("ext2_fopen: '%s' is a directory\n", path);
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

size_t ext2_fread(void *ptr, size_t size, size_t nmemb, ext2_FILE *stream)
{
    if (!stream) return 0;
    if (stream->mode != 0)
    {
        printf("ext2_fread: file not opened for reading\n");
        return 0;
    }

    size_t total = size * nmemb;
    if (total == 0) return 0;

    if (stream->pos >= stream->inode.i_size)
    {
        return 0;
    }

    size_t remain = stream->inode.i_size - stream->pos;
    if (remain < total)
    {
        total = remain;
    }

    if (stream->inode.i_block[0] == 0)
    {
        return 0;
    }

    uint8_t blockbuf[EXT2_BLOCK_SIZE];
    ext2_read_block(stream->inode.i_block[0], blockbuf);

    memcpy(ptr, blockbuf + stream->pos, total);

    stream->pos += total;

    return total / size;
}

size_t ext2_fwrite(const void *ptr, size_t size, size_t nmemb, ext2_FILE *stream)
{
    if (!stream) return 0;
    if (stream->mode != 1)
    {
        printf("ext2_fwrite: file not opened for writing\n");
        return 0;
    }

    size_t total = size * nmemb;
    if (total == 0) return 0;

    if (stream->inode.i_block[0] == 0)
    {
        uint32_t new_block = ext2_allocate_block();
        if (!new_block)
        {
            printf("ext2_fwrite: no free block\n");
            return 0;
        }
        stream->inode.i_block[0] = new_block;
        stream->inode.i_blocks = 2;
    }

    uint8_t blockbuf[EXT2_BLOCK_SIZE];
    ext2_read_block(stream->inode.i_block[0], blockbuf);

    size_t space = EXT2_BLOCK_SIZE - stream->pos;
    if (space < total)
    {
        total = space;
    }

    memcpy(blockbuf + stream->pos, ptr, total);

    ext2_write_block(stream->inode.i_block[0], blockbuf);

    stream->pos += total;
    if (stream->pos > stream->inode.i_size)
    {
        stream->inode.i_size = stream->pos;
    }
    ext2_write_inode(stream->inode_num, &stream->inode);

    return total / size; /* number of "elements" written */
}

/* --- Fileio fds Implementations --- */
int sys_open(const char *path, int flags)
{
    task_t *current = get_current_task();
    int fd;
    char mode_str[4];
    ext2_FILE *fp;

    /* Find a free file descriptor slot */
    for (fd = 3; fd < MAX_FDS; fd++)
    {
        if (current->fd_table[fd] == false)
            break;
    }
    if (fd == MAX_FDS)
    {
        printf("sys_open: too many open files\n");
        return -1;
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
        current->fd_pointers[fd].type = FD_MODULE;
    else
        current->fd_pointers[fd].type = FD_FILE;
    current->fd_pointers[fd].flags = flags;
    current->fd_pointers[fd].file = fp;
    current->fd_pointers[fd].offset = fp->pos;
    current->fd_pointers[fd].ref_count = 1;
    current->fd_table[fd] = true;

    return fd;
}

int sys_close(int fd)
{
    task_t* current;
    file_t* file_obj;
    
    current = get_current_task();
    if (fd < 0 || fd >= MAX_FDS || current->fd_table[fd] == false)
        return -1;

    /* Get pointer to the file object in the array */
    file_obj = &current->fd_pointers[fd];
    if (file_obj->type == FD_SOCKET)
    {
        socket_close(file_obj->socket);
    }
    else
        ext2_fclose(file_obj->file);
    
    /* Mark slot as free and zero out the structure */
    current->fd_table[fd] = false;
    memset(&current->fd_pointers[fd], 0, sizeof(file_t));
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
    if (file_obj->type == FD_SOCKET)
    {
        return socket_recv(file_obj->socket, buf, count);
    }
    else if (file_obj->type == FD_MODULE)
    {
        int mod_num;
        n = ext2_fread(&mod_num, 4, 1, file_obj->file);
        /* TODO
         * With modnum, simply call the read function of the module
         */
        module_t *mod = file_obj->module;

        if (mod && mod->read)
        {
            size_t offset = file_obj->offset;
            mod->read(mod, buf, count, &offset);
            file_obj->offset = offset;
            return count;
        }
        return -1;
    }

    n = ext2_fread(buf, 1, count, file_obj->file);
    file_obj->offset = file_obj->file->pos;
    return n;
}

ssize_t sys_write(int fd, const void *buf, size_t count)
{
    task_t *current;
    file_t *file_obj;
    size_t n;

    if (fd == 1)
    {
        puts(buf);
        return count;
    }

    current = get_current_task();
    if (fd < 0 || fd >= MAX_FDS || current->fd_table[fd] == false)
        return -1;

    file_obj = &current->fd_pointers[fd];
    if (file_obj->type == FD_SOCKET)
    {
        return socket_send(file_obj->socket, buf, count);
    }

    n = ext2_fwrite(buf, 1, count, file_obj->file);
    file_obj->offset = file_obj->file->pos;
    return n;
}

/* --- Command Implementations --- */
void ext2_cmd_ls(const char *path)
{
    uint32_t inode_num;
    if (ext2_resolve_path(path, &inode_num) < 0)
    {
        printf("Directory not found\n");
        return;
    }
    struct ext2_inode dir;
    ext2_read_inode(inode_num, &dir);
    if (!(dir.i_mode & DIR_MODE))
    {
        printf("Not a directory\n");
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
            printf("%s  ", name);
        }
        offset += de->rec_len;
    }
    printf("\n");
    kfree(blkbuf);
}

void ext2_cmd_cat(const char *path)
{
    uint32_t inode_num;
    if (ext2_resolve_path(path, &inode_num) < 0)
    {
        printf("File not found\n");
        return;
    }
    struct ext2_inode file;
    ext2_read_inode(inode_num, &file);
    if (file.i_mode & DIR_MODE)
    {
        printf("Is a directory\n");
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
        printf("Parent directory not found\n");
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
        printf("Directory '%s' already exists\n", path);
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
        printf("Parent directory not found\n");
        return;
    }
    uint32_t new_inode = ext2_allocate_inode();
    if (new_inode == 0)
    {
        printf("No free inode available\n");
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
        printf("No free block available\n");
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
        printf("File not found\n");
        return;
    }
    struct ext2_inode file;
    ext2_read_inode(inode_num, &file);
    if (file.i_mode & DIR_MODE)
    {
        printf("Is a directory – use rmdir\n");
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
        printf("Parent directory not found\n");
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
        printf("Directory not found\n");
        return;
    }
    struct ext2_inode dir;
    ext2_read_inode(inode_num, &dir);
    if (!(dir.i_mode & DIR_MODE))
    {
        printf("Not a directory\n");
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
        printf("Directory not empty\n");
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
        printf("Parent directory not found\n");
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
        printf("Directory not found\n");
        return;
    }
    struct ext2_inode inode;
    ext2_read_inode(inode_num, &inode);
    if (!(inode.i_mode & DIR_MODE))
    {
        printf("Not a directory\n");
        return;
    }
    current_dir = inode_num;
}

void ext2_cmd_cp(const char *src_path, const char *dst_path)
{
    uint32_t src_inode_num;
    if (ext2_resolve_path(src_path, &src_inode_num) < 0)
    {
        printf("cp: source not found: %s\n", src_path);
        return;
    }

    struct ext2_inode src_inode;
    ext2_read_inode(src_inode_num, &src_inode);

    if (src_inode.i_mode & DIR_MODE)
    {
        printf("cp: source is a directory (not supported)\n");
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
        printf("cp: destination directory not found: %s\n", parent_path);
        kfree(data_buf);
        return;
    }

    uint32_t new_inode_num = ext2_create_file(parent_inode_num, file_name, FILE_MODE /* regular file */);
    if (new_inode_num == 0)
    {
        printf("cp: failed to create destination file: %s\n", dst_path);
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
            printf("cp: no free block available\n");
            kfree(data_buf);
            return;
        }
        new_inode.i_block[0] = new_block;
        new_inode.i_blocks = 2; // 1 block = 2 in 512-byte units
        ext2_write_block(new_block, data_buf);
    }
    ext2_write_inode(new_inode_num, &new_inode);

    kfree(data_buf);
    printf("cp: copied '%s' to '%s'\n", src_path, dst_path);
}

void ext2_cmd_mv(const char *src_path, const char *dst_path)
{
    uint32_t src_inode_num;
    if (ext2_resolve_path(src_path, &src_inode_num) < 0)
    {
        printf("mv: source not found: %s\n", src_path);
        return;
    }

    char src_parent[256], src_name[256];
    split_path(src_path, src_parent, src_name);

    char dst_parent[256], dst_name[256];
    split_path(dst_path, dst_parent, dst_name);

    uint32_t dst_parent_inode;
    if (ext2_resolve_path(dst_parent, &dst_parent_inode) < 0)
    {
        printf("mv: destination directory not found: %s\n", dst_parent);
        return;
    }

    uint32_t dst_inode_num;
    if (ext2_resolve_path(dst_path, &dst_inode_num) == 0)
    {
        // For simplicity, we fail if destination exists
        // (In real Unix, we'd remove it if it's a file, or fail if it's a non-empty directory)
        printf("mv: destination already exists: %s\n", dst_path);
        return;
    }

    struct ext2_inode src_inode;
    ext2_read_inode(src_inode_num, &src_inode);
    uint8_t file_type = (src_inode.i_mode & DIR_MODE) ? EXT2_FT_DIR : EXT2_FT_REG_FILE;
    if (ext2_add_dir_entry(dst_parent_inode, dst_name, src_inode_num, file_type) < 0)
    {
        printf("mv: failed to create destination entry\n");
        return;
    }

    uint32_t src_parent_inode;
    if (ext2_resolve_path(src_parent, &src_parent_inode) < 0)
    {
        printf("mv: source parent not found?!\n");
        return;
    }
    if (ext2_remove_dir_entry(src_parent_inode, src_name) < 0)
    {
        printf("mv: failed to remove old directory entry\n");
        return;
    }

    printf("mv: moved '%s' to '%s'\n", src_path, dst_path);
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
    printf("Enter the file name: ");
    ext2_cmd_cat(get_line());
}

static void cmd_touch()
{
    printf("Enter the file name: ");
    ext2_cmd_touch(get_line());
}

static void cmd_mkdir()
{
    printf("Enter the directory name: ");
    ext2_cmd_mkdir(get_line());
}

static void cmd_rm()
{
    printf("Enter the file name: ");
    ext2_cmd_rm(get_line());
}

static void cmd_rmdir()
{
    printf("Enter the directory name: ");
    ext2_cmd_rmdir(get_line());
}

static void cmd_cd()
{
    printf("Enter the directory name: ");
    ext2_cmd_cd(get_line());
}
static void cmd_cp()
{
    printf("Enter source file: ");
    char *src = get_line();
    printf("Enter destination: ");
    char *dst = get_line();
    ext2_cmd_cp(src, dst);
}

static void cmd_mv()
{
    printf("Enter source file: ");
    char *src = get_line();
    printf("Enter destination: ");
    char *dst = get_line();
    ext2_cmd_mv(src, dst);
}

static void cmd_pwd(void)
{
    char *cwd = ext2_pwd();
    if (cwd)
    {
        printf("%s\n", cwd);
        kfree(cwd);
    }
    else
    {
        printf("pwd: error retrieving current directory\n");
    }
}

static void cmd_tfds(void)
{
    int fd;

    fd = sys_open("/etc/users.config", O_RDONLY);
    if (fd < 0)
    {
        printf("Failed to open /etc/users.config\n");
        return;
    }

    char buf[256];
    ssize_t n = sys_read(fd, buf, sizeof(buf));
    if (n < 0)
    {
        printf("Failed to read /etc/users.config\n");
        sys_close(fd);
        return;
    }

    printf("Read %d bytes from /etc/users.config:\n", n);
    for (int i = 0; i < n; i++)
        putc(buf[i]);
    sys_close(fd);
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
        printf("Failed to open hello.txt for writing\n");
        return;
    }

    sys_write(fd, msg, strlen(msg));
    sys_close(fd);

    printf("Wrote to hello.txt, fd %d\n", fd);

    fd = sys_open("helloTFD.txt", O_RDONLY);
    if (fd < 0)
    {
        printf("Failed to open hello.txt for reading\n");
        return;
    }
    memset(buf, 0, sizeof(buf));
    n = sys_read(fd, buf, sizeof(buf) - 1);

    printf("Read from fd(%d) '%d' bytes: %s", fd, n, buf);
    
    if (strcmp(buf, msg) != 0)
    {
        printf("Failed to read back what was written\n");
        sys_close(fd);
        return;
    }
    printf("Read %u bytes: %s\n", (unsigned)n, buf);
    sys_close(fd);
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
}

void test_fileio()
{
    ext2_FILE *f = ext2_fopen("hello.txt", "w");
    if (!f)
    {
        printf("Failed to open hello.txt for writing\n");
        return;
    }
    const char *msg = "Hello from ext12_fwrite!\n";
    ext2_fwrite(msg, 1, strlen(msg), f);
    ext2_fclose(f);

    f = ext2_fopen("hello.txt", "r");
    if (!f)
    {
        printf("Failed to open hello.txt for reading\n");
        return;
    }
    char buf[128];
    memset(buf, 0, sizeof(buf));
    size_t n = ext2_fread(buf, 1, sizeof(buf) - 1, f);
    printf("Read %u bytes: %s\n", (unsigned)n, buf);
    ext2_fclose(f);
}

void ext2_mount(void)
{
    uint8_t* buf = kmalloc(EXT2_BLOCK_SIZE);
    /* Read superblock (located at block 1) */
    ext2_read_block(1, buf);
    memcpy(&ext2.sb, buf, sizeof(struct ext2_super_block));
    if (ext2.sb.s_magic != 0xEF53)
        kernel_panic("ext2: bad magic number");
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
