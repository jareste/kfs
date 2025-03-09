#include "../display/display.h"
#include "../keyboard/keyboard.h"
#include "../keyboard/signals.h"
#include "../utils/utils.h"
#include "../time/time.h"
#include "../umgmnt/users.h"
#include "../ide/fs.h"
#include "kshell.h"
#include "../../srcs/user/syscalls/stdlib.h"

#include "../ide/ext2_fileio.h"

#define MAX_COMMANDS 256
#define MAX_SECTIONS_COMMANDS 25
#define MAX_SECTIONS SECTION_T_MAX

typedef struct
{
    const char* name;
    section_t section;
    command_t commands[MAX_SECTIONS_COMMANDS];
} command_section_t;

static section_t current_section = GENERAL;

static void help();
static void ks_kdump();
static void reboot();
static void kdump_stack();
static void halt();
static void hhalt();
static void color();
static void shutdown();
static void ksleep();
static void kuptime();
static void cmd_section();
static void help_global();
static void ks_kill();
static void trigger_interrupt_software_0();
static void trigger_interrupt_software_6();
static void test_syscall();
static void set_layout();
static void ks_signal();
static void ks_get_pid();

static void ks_read_mod_kb();

static command_section_t command_sections[MAX_SECTIONS];

static command_t global_commands[] = {
    {"help", "Display this help message", help},
    {"?", NULL, help},
    {"h", NULL, help_global},
    {"reboot", "Reboot the system", reboot},
    {"clear", "Clear the screen", clear_screen},
    {"shutdown", "Shutdown the system", shutdown},
    {"sec", "Change the command section", cmd_section},
    {"readmodkb", "Read from the keyboard module", ks_read_mod_kb},
    {NULL, NULL, NULL}    
};

static command_t in_commands[] = {
    {"exit", "Exit the shell", NULL},
    {"color", "Set shell color", color},
    {"uptime", "Get the system uptime in seconds.", kuptime},
    {"layout", "Set keyboard layout", set_layout},
    {"mypid", "Get the PID of the current task", ks_get_pid},
    {NULL, NULL, NULL}
};

static command_t dcommand[] = {
    {"kdump", "Dump memory", ks_kdump},
    {"stack", "Dump stack", kdump_stack},
    {"halt", "Halt the system.", halt},
    {"sudo halt", "Halt the system. (Stops it).", hhalt},
    {"reg", "Dumps Registers", dump_registers},
    {"sleep", "Sleeps the kernel for 'n' seconds", ksleep},
    {"raise0", "Raise a division by zero exception", trigger_interrupt_software_0},
    {"raise6", "Raise an invalid opcode exception", trigger_interrupt_software_6},
    {"syscall", "Test syscalls", test_syscall},
    {NULL, NULL, NULL}
};

static command_t tcommand[] = {
    {"kill", "Kill a task", ks_kill},
    {"signal", "Send a signal to a task", ks_signal},
    {NULL, NULL, NULL}
};

static void install_command(command_t* cmds, const char* cmd, const char* desc, void (*func)())
{
    int i;

    for (i = 0; i < MAX_SECTIONS_COMMANDS; i++)
    {
        if (cmds[i].cmd == NULL)
        {
            cmds[i].cmd = cmd;
            cmds[i].desc = desc;
            cmds[i].func = func;
            return;
        }
    }
}

void install_all_cmds(command_t* cmds, section_t section)
{
    int i;

    for (i = 0; i < MAX_SECTIONS_COMMANDS; i++)
    {
        if (cmds[i].cmd == NULL)
            break;
        
        install_command(command_sections[section].commands,\
         cmds[i].cmd, cmds[i].desc, cmds[i].func);
    }
}

static void init_section(const char* name, section_t section)
{
    command_sections[section].name = name;
    command_sections[section].section = section;
    memset(command_sections[section].commands, 0,\
    sizeof(command_sections[section].commands));
}

void init_kshell()
{
    init_section("global", GLOBAL);
    init_section("general", GENERAL);
    init_section("memory", MEMORY);
    init_section("debug", DEBUG);
    init_section("tasks", TASKS);

    current_section = GLOBAL;

    install_all_cmds(global_commands, GLOBAL);
    install_all_cmds(in_commands, GENERAL);
    install_all_cmds(dcommand, DEBUG);
    install_all_cmds(tcommand, TASKS);
}

static void kuptime()
{
    uint64_t uptime;
    uptime = get_kuptime();
    printf("Uptime: %d seconds\n", uptime);
}

static void ksleep()
{
    // fs_node_t* node;
    // node = fs_build_tree_from_inode(2, "/", NULL, NULL);
    // if (node)
    // {
    //     fs_print_tree(node, 0);
    //     fs_destroy_tree(node);
    // }
    // char* buffer;
    // uint32_t seconds;

    // printf("Enter the number of seconds to sleep: ");
    
    // buffer = get_line();

    // seconds = (uint32_t)hex_string_to_int(buffer);
    // printf("Sleeping for %d seconds...\n", seconds);
    // sleep(seconds);
    // printf("Woke up!\n");
}

static void ks_kill()
{
    char* buffer;
    pid_t pid;
    int signal;

    printf("Enter the PID to kill: ");
    buffer = get_line();
    pid = (pid_t)hex_string_to_int(buffer);
    printf("Enter the signal to send: ");

    buffer = get_line();

    signal = (int)hex_string_to_int(buffer);
    printf("Killing PID: %d with signal: %d\n", pid, signal);
    kill(pid, signal); /* this kill is the one wrapped into interrupt */
}

static void task_1_sighandler(int signal)
{
    puts_color("Task 1 received signal\n", RED);
}

static void ks_signal()
{
    char* buffer;
    pid_t pid;
    int signum;

    printf("Enter the PID to kill: ");
    buffer = get_line();
    pid = (pid_t)hex_string_to_int(buffer);
    printf("Enter the signal to send: ");

    buffer = get_line();

    signum = (int)hex_string_to_int(buffer);
    printf("signaling PID: %d with signal: %d\n", pid, signal);
    signal(pid, task_1_sighandler); /* this kill is the one wrapped into interrupt */
}

static void color()
{
    char* buffer;
    uint32_t color;

    puts("Available colors:\n");
    puts("  0: BLACK\n");
    puts("  1: BLUE\n");
    puts("  2: GREEN\n");
    puts("  3: CYAN\n");
    puts("  4: RED\n");
    puts("  5: MAGENTA\n");
    puts("  6: BROWN\n");
    puts("  7: LIGHT_GREY\n");
    puts("  8: DARK_GREY\n");
    puts("  9: LIGHT_BLUE\n");
    puts("  A: LIGHT_GREEN\n");
    puts("  B: LIGHT_CYAN\n");
    puts("  C: LIGHT_RED\n");
    puts("  D: LIGHT_MAGENTA\n");
    puts("  E: LIGHT_BROWN\n");
    puts("  F: WHITE\n");
    puts("Enter the color: ");

    buffer = get_line();

    color = (uint32_t)hex_string_to_int(buffer);
    printf("Color: %x\n", color);
    set_putchar_color(color);
    clear_kb_buffer();
}

static void halt()
{
    puts("Halting system...\n");
    __asm__ __volatile__("hlt");
    puts("System halted\n");
}

static void hhalt()
{
    puts("Halting system... Safe reboot now.\n");
    __asm__ __volatile__("cli; hlt");
    puts("System halted\n");
}

static void help()
{
    puts("Available commands:\n");
    
    command_t* commands = command_sections[current_section].commands;

    for (int i = 0; i < MAX_SECTIONS_COMMANDS; i++)
    {
        if (commands[i].desc == NULL)
            continue;

        printf("  %s: %s\n", commands[i].cmd, commands[i].desc);
    }

}

static void help_global()
{
    puts("Available commands:\n");
    
    command_t* commands = command_sections[GLOBAL].commands;

    for (int i = 0; i < MAX_SECTIONS_COMMANDS; i++)
    {
        if (commands[i].desc == NULL)
            continue;

        printf("  %s: %s\n", commands[i].cmd, commands[i].desc);
    }

}

static void kdump_stack()
{
    kdump((void*)get_stack_pointer(), 256);
}

static void ks_kdump()
{
    char* buffer;
    uint32_t addr;
    uint32_t size;

    printf("Known addresses:\n");
    printf("  VIDEO_MEMORY: %x\n", VIDEO_MEMORY);
    printf("  BANNER: %x\n", &BANNER);
    printf("Enter the address to dump: ");
    buffer = get_line();
    
    addr = (uint32_t)hex_string_to_int(buffer);
    printf("Address: %x\n", addr);
    
    printf("Enter the size to dump: ");
    buffer = get_line();
    size = (uint32_t)hex_string_to_int(buffer);
    printf("Size: %x\n", size);

    if (size < 0 || size > 0x1000)
    {
        printf("Invalid size. Size must be between 0 and 0x1000\n");
        return;
    }

    clear_kb_buffer();
    
    kdump((void*)addr, size);
}

static void reboot()
{
    outb(0x64, 0xFE);
}

static void shutdown()
{
    puts("Shutting down...\n");
    
    /* Delay for shutdown. */
    for (int i = 0; i < 50; i++) __asm__ __volatile__("hlt");

    /* QEMU-specific: sends shutdown signal to ACPI */
    outw(0x604, 0x2000);
}

static void cmd_section()
{
    int i;
    char* buffer;
    uint32_t section;

    puts("Available sections:\n");
    for (i = 0; i < MAX_SECTIONS; i++)
    {
        printf("  %d: %s\n", i, command_sections[i].name);
    }
    puts("Enter the section: ");
    buffer = get_line();
    section = (uint32_t)hex_string_to_int(buffer);
    if (section < 0 || section >= MAX_SECTIONS)
    {
        puts("Invalid section\n");
        return;
    }
    printf("Section: %s\n", command_sections[section].name);
    current_section = section;
}

static bool check_global_cmd(char* cmd)
{
    command_t* commands = command_sections[GLOBAL].commands;
    for (int i = 0; i < MAX_SECTIONS_COMMANDS; i++)
    {
        if (commands[i].cmd && (strcmp(cmd, commands[i].cmd) == 0))
        {
            if (commands[i].func)
            {
                commands[i].func();
            }
            else
            {
                printf("Not implemented yet\n");
            }
            return true;
        }
    }
    return false;
}

static void set_layout()
{
    char* buffer;
    uint32_t layout;

    puts("Available layouts:\n");
    puts("  0: QWERTY_ENG\n");
    puts("  1: QWERTY_ES\n");
    puts("  2: AZERTY_FR\n");
    puts("  3: QWERTZ_DE\n");
    puts("Enter the layout: ");
    buffer = get_line();
    layout = (uint32_t)hex_string_to_int(buffer);
    if (layout < 0 || layout >= MAX_KEYBOARD)
    {
        puts("Invalid layout\n");
        return;
    }
    printf("Layout: %d\n", layout);
    set_keyboard_layout(layout);
}

static void trigger_interrupt_software_0()
{
    asm volatile (
        "mov $3, %eax\n"
        "xor %edx, %edx\n"
        "div %edx\n"
    );
}

static void trigger_interrupt_software_6()
{    
    int j = 3;
    int i = 0;
    printf("Kernel end: %d\n", j / i);
}

static void ks_get_pid()
{
    printf("PID: %d\n", get_pid());
}

void test_syscall_read()
{
    int return_value;
    char buffer[10];

    memset(buffer, 0, sizeof(buffer));
    puts_color("TEST_READ\n", LIGHT_MAGENTA);
    printf("Buffer before sys_read: %s\n", buffer);

    asm volatile (
        "mov $2, %%eax\n"
        "mov $0, %%ebx\n"
        "mov %1, %%ecx\n"
        "mov %2, %%edx\n"
        "int $0x80\n"
        "mov %%eax, %0\n"
        : "=r"(return_value)
        : "r"(buffer), "r"(sizeof(buffer))
        : "eax", "ebx", "ecx", "edx"
    );

    printf("Buffer after sys_read: '");
    for (size_t i = 0; i < 10; i++)
    {
        if (buffer[i] == 0)
            break;
        putc(buffer[i]);
    }
    puts("'\n");
    printf("SYS_READ return value: %d\n", return_value);
}

void test_syscall()
{
    int return_value;
    puts_color("TEST_EXIT\n", LIGHT_MAGENTA);
    
    asm volatile (
        "mov $0, %%eax\n"
        "mov $42, %%ebx\n"
        "int $0x80\n"
        "mov %%eax, %0\n"
        : "=r"(return_value)
        :
        : "eax", "ebx"
    );
    printf("SYS_EXIT return value: %d\n", return_value);

    const char* msg = "Hello, world!\n";
    size_t msg_len = strlen(msg);

    puts_color("TEST_WRITE\n", LIGHT_MAGENTA);
    return_value = write(1, msg, msg_len);
    printf("SYS_WRITE return value: %d\n", return_value);
    
    test_syscall_read();
}

static void ks_read_mod_kb()
{
    char buffer[256];
    size_t offset = 0;
    size_t n;
    int fd;
    puts_color("Reading from keyboard module...\n", LIGHT_MAGENTA);
    while (1)
    {
        fd = sys_open("/dev/keyboard_module", O_RDONLY);
        n = sys_read(fd, buffer, sizeof(buffer) - 1);
        buffer[n] = 0;
        printf("Read from keyboard module: '%s'\n", buffer);
        if (strcmp(buffer, "exit") == 0)
            break;
    }
}

void kshell()
{
    int i = 0;
    command_t* commands;
    char* buffer;
    // tty_init();
    set_keyboard_layout(QWERTY_ENG);
    print_date();
    while (1)
    {
        scheduler();
        tty_save_to_file("/dev/tty");
        printf("jareste-OS> ");
        buffer = get_line();

        if (*buffer == 0)
            continue;

        /* Comment this for working without wanting to burn the computer
         */
        // if (!current_user_is_valid() && strcmp(buffer, "login") != 0)
        // {
        //     printf("Please login first\n");
        //     continue;
        // }

        if (check_global_cmd(buffer))
            continue;

        commands = command_sections[current_section].commands;
        for (i = 0; i < MAX_SECTIONS_COMMANDS; i++)
        {
            if (commands[i].cmd && (strcmp(buffer, commands[i].cmd) == 0))
            {
                if (commands[i].func)
                    commands[i].func();
                else
                    printf("Not implemented yet\n");
                break;
            }
        }
        if (i == MAX_SECTIONS_COMMANDS)
            printf("Command '%s' not found. Type 'help' for a list of available commands\n",\
                buffer, command_sections[current_section].name, current_section);
    }
}

