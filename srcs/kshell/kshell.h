#ifndef KSHELL_H
#define KSHELL_H

typedef struct
{
    const char* cmd;
    const char* desc;
    void (*func)();
} command_t;

typedef enum {
    GLOBAL = 0,
    GENERAL,
    MEMORY,
    DEBUG,
    TASKS,
    MODULES,
    SECTION_T_MAX
} section_t;

void kshell();
void init_kshell();
// void install_command(const char* cmd, const char* desc, void (*func)());
void install_all_cmds(command_t* cmds, section_t section);

#endif
