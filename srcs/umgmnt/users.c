#include "users.h"
#include "../ide/ext2.h"
#include "../ide/ext2_fileio.h"
#include "../utils/utils.h"
#include "../utils/stdint.h"

#include "../display/display.h"

bool find_user_by_name(const char *name, user_t* u)
{
    ext2_FILE *fp;

    fp = ext2_fopen(USERS_CONFIG, "r");
    if (!fp)
    {
        // printf("Config file not found.\n");
        // if (strcmp(name, NO_USER_LOGIN) == 0)
        // {
        //     strcpy(u->name, "root");
        //     strcpy(u->pass_hash, "foo");
        //     u->uid = 0;
        //     u->gid = 0;
        //     u->home_inode = 2;
        //     u->shell_inode = 2;
        //     u->is_valid = true;
        //     printf("Login as root.\n");
        //     return true;
        // }
        return false;
    }

    while (ext2_fread(fp, u, sizeof(user_t)))
    {
        if (u->is_valid && strcmp(u->name, name) == 0)
        {
            ext2_fclose(fp);
            return true;
        }
    }

    ext2_fclose(fp);
    // printf("User '%s' not found.\n", name);
    return false;
}

bool user_exists(const char *name)
{
    user_t u;
    return find_user_by_name(name, &u);
}

void add_user(user_t *new_user)
{
    ext2_FILE *fp;

    if (user_exists(new_user->name))
    {
        printf("User '%s' already exists.\n", new_user->name);
        return;
    }
    fp = ext2_fopen(USERS_CONFIG, "a");
    if (!fp)
    {
        return;
    }
    ext2_fwrite(fp, new_user, sizeof(user_t));
    ext2_fclose(fp);
}

void list_users()
{
    ext2_FILE *fp;
    user_t u;

    fp = ext2_fopen(USERS_CONFIG, "r");
    if (!fp)
    {
        return;
    }

    while (ext2_fread(fp, &u, sizeof(user_t)))
    {
        printf("User: '%s' %s\n", u.name, u.is_valid ? "active" : "deleted");
    }

    ext2_fclose(fp);
}
