#include "fs.h"
#include "../memory/memory.h"
#include "../utils/utils.h"
#include "ext2.h"
#include "../display/display.h"

#define EXT2_BLOCK_SIZE 1024

fs_node_t* fs_create_node(const char* name, uint32_t size, fs_node_type_t type,
                          uint32_t inode, uint16_t links, uint16_t rights)
{
    fs_node_t *node = kmalloc(sizeof(fs_node_t));
    if (!node)
        return NULL;
    
    node->name = kmalloc(strlen(name) + 1);
    if (!node->name)
    {
        kfree(node);
        return NULL;
    }
    strcpy(node->name, name);
    
    node->size = size;
    node->type = type;
    node->inode = inode;
    node->links = links;
    node->rights = rights;
    
    node->master = NULL;
    node->father = NULL;
    node->children = NULL;
    node->next_of_kin = NULL;
    
    return node;
}

void fs_destroy_tree(fs_node_t* node)
{
    if (!node)
        return;
    
    fs_node_t* child = node->children;
    while(child)
    {
        fs_node_t* next = child->next_of_kin;
        fs_destroy_tree(child);
        child = next;
    }

    kfree(node->name);
    kfree(node);
}

void fs_print_tree(fs_node_t* node, int indent)
{
    if (!node)
        return;
    
    for (int i = 0; i < indent; i++)
        kprintf("  ");
    
    kprintf("%s (inode: %d, size: %d)\n", node->name, node->inode, node->size);
    
    if (node->children)
        fs_print_tree(node->children, indent + 1);
    
    if (node->next_of_kin)
        fs_print_tree(node->next_of_kin, indent);
}

fs_node_t* fs_build_tree_from_inode(uint32_t inode_num, const char* name,
                                      fs_node_t* father, fs_node_t* master)
{
    ext2_inode_t inode;
    if (ext2_read_inode(inode_num, &inode) < 0)
    {
        kprintf("Error reading inode %d\n", inode_num);
        return NULL;
    }
    
    fs_node_type_t type;
    if (S_ISDIR(inode.i_mode))
        type = FS_NODE_DIR;
    else if (S_ISREG(inode.i_mode))
        type = FS_NODE_FILE;
    else
        type = FS_NODE_UNKNOWN;
    
    fs_node_t* node = fs_create_node(name, inode.i_size, type, inode_num,
                                     inode.i_links_count, inode.i_mode);
    if (!node)
        return NULL;
    
    node->father = father;
    node->master = (father == NULL) ? node : master;
    
    if (type == FS_NODE_DIR)
    {
        if (inode.i_block[0] == 0)
        {
            return node;
        }
        
        uint8_t block_buf[EXT2_BLOCK_SIZE];
        read_block(inode.i_block[0], block_buf);
        
        uint32_t offset = 0;
        fs_node_t* first_child = NULL;
        fs_node_t* prev_child = NULL;
        
        while (offset < inode.i_size)
        {
            ext2_dir_entry_t* dentry = (ext2_dir_entry_t*)(block_buf + offset);
            if (dentry->inode != 0)
            {
                if ((dentry->name_len == 1 && dentry->name[0] == '.') ||
                    (dentry->name_len == 2 && dentry->name[0] == '.' && dentry->name[1] == '.'))
                {
                    /* Do nothing */
                }
                else
                {
                    char child_name[MAX_NAME_LENGTH];
                    memset(child_name, 0, MAX_NAME_LENGTH);
                    memcpy(child_name, dentry->name, dentry->name_len);
                    child_name[dentry->name_len] = '\0';
                    
                    fs_node_t* child_node = fs_build_tree_from_inode(dentry->inode, child_name,
                                                                       node,
                                                                       (father == NULL) ? node : master);
                    if (child_node)
                    {
                        if (first_child == NULL)
                        {
                            first_child = child_node;
                            node->children = child_node;
                        }
                        else
                        {
                            prev_child->next_of_kin = child_node;
                        }
                        prev_child = child_node;
                    }
                }
            }
            offset += dentry->rec_len;
        }
    }
    
    return node;
}
