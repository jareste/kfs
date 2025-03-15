#include "users.h"
#include "../utils/utils.h"
#include "../display/display.h"
#include "../kshell/kshell.h"
#include "../keyboard/keyboard.h"
#include "../tasks/task.h"

static void cmd_logout();
static void cmd_login();
static void cmd_create_user();

static command_t users_commands[] = {
    {"login", "Login to the system", cmd_login},
    {"logout", "Logout from the system", cmd_logout},
    {"cruser", "Create a new user", cmd_create_user},
    {NULL, NULL, NULL}
};

static user_t g_current_user;

bool current_user_is_valid()
{
    return g_current_user.is_valid;
}

static int login(char *username, char *password)
{
    user_t u;

    if (find_user_by_name(username, &u) == false)
    {
        return -1;
    }

    if (check_password(password, u.pass_hash) == 0)
    {
        printf("Invalid password.\n");
        return -1;
    }

    memcpy(&g_current_user, &u, sizeof(user_t));
    printf("Login successful as '%s'.\n", username);
    get_current_task()->state = TASK_WAITING;
    start_user();

    set_current_dir(g_current_user.home_inode);
    clear_kb_buffer();
    return 0;
}

static void cmd_login()
{
    char* buffer;
    char username[MAX_USER_NAME];
    char password[SHA256_HEX_LEN + 1];
 
    printf("Username: ");
    buffer = get_line();
    strcpy(username, buffer);
    printf("Password: ");
    start_ofuscation();
    buffer = get_line();
    stop_ofuscation();
    strcpy(password, buffer);

    login(username, password);
}

static void cmd_logout()
{
    printf("Logout done!\n");
}

static void cmd_create_user()
{
    user_t u;
    char buffer[SHA256_HEX_LEN + 1];
    char *pass;
    char path[256];

    printf("Enter username: ");
    strcpy(u.name, get_line());
    if (user_exists(u.name))
    {
        printf("User '%s' already exists.\n", u.name);
        return;
    }

    printf("Enter password: ");
    start_ofuscation();
    pass = get_line();
    stop_ofuscation();
    encrypt_password(pass, buffer);
    strcpy(u.pass_hash, buffer);

    printf("Enter UID: ");
    u.uid = strtol(get_line(), NULL, 10);
    printf("Enter GID: ");
    u.gid = strtol(get_line(), NULL, 10);
    printf("Enter home dir: ");
    u.home_inode = ext2_get_inode(get_line());
    u.home_inode = u.home_inode == 0 ? 2 : u.home_inode;
    printf("Home inode: %d\n", u.home_inode);
    printf("Enter shell inode: ");
    u.shell_inode = strtol(get_line(), NULL, 10);
    u.is_valid = true;

    strcpy(path, "/home/");
    strcat(path, u.name);

    ext2_cmd_mkdir(path);

    add_user(&u);
}

void init_users_api()
{
    install_all_cmds(users_commands, GLOBAL);
    g_current_user.is_valid = false;
}
