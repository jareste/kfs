#include "ushell.h"
#include "../syscalls/stdlib.h"
#include "../../utils/utils.h"

#include "../../display/display.h"

#define MAX_TOKENS 15

#define MAX_COMMANDS 256
#define MAX_SECTIONS_COMMANDS 25
#define MAX_SECTIONS SECTION_T_MAX

void echo(char** args, int argc);
void u_exit(char** argv, int argc);
void u_sleep(char** argv, int argc);
void u_usleep(char** argv, int argc);
void u_time(char** argv, int argc);

typedef enum
{
    ECHO = 0,
    EXIT,
    USLEEP,
    SLEEP,
    TIME,
    BUILTIN_MAX
} builtin_def;

typedef struct
{
    const char* cmd;
    builtin_def def;
} builtin_t;

typedef struct
{
    void (*handler)(char**, int);
} user_cmd_t;

static user_cmd_t user_cmds[BUILTIN_MAX];

static builtin_t builtins[] = {
    {"echo", ECHO},
    {"exit", EXIT},
    {"usleep", USLEEP},
    {"sleep", SLEEP},
    {"time", TIME},
    {NULL, 0}
};

static char m_buffer[1024];

char* readline(char* prompt)
{
    int bytes_read;
    char buffer[1024];

    if (!prompt)
        prompt = "$ ";

    write(1, prompt, strlen(prompt));

    memset(m_buffer, 0, 1024);

    bytes_read = read(0, buffer, 1024);
    if (bytes_read <= 0)
        return NULL;

    memcpy(m_buffer, buffer, bytes_read);
    m_buffer[bytes_read] = '\0';
    return m_buffer;
}

void exec_builtin(char** argv, int argc)
{
    int i;

    i = 0;
    while (builtins[i].cmd != NULL)
    {
        if (strcmp(argv[0], builtins[i].cmd) == 0)
        {

            user_cmds[builtins[i].def].handler(argv, argc);
            // switch (builtins[i].def)
            // {
            //     case ECHO:
            //         echo(argv, argc);
            //         break;
            //     case EXIT:
            //         u_exit(argv, argc);
            //         break;
            //     default:
            //         break;
            // }
            return;
        }
        i++;
    }

    printf("Command '%s' not found\n", argv[0]);
}

void ushell(char** envp)
{
    char* buffer;
    char* tokens[MAX_TOKENS];
    // char* p;
    int token_count;
    int len;
    int i;

    user_cmds[ECHO].handler = echo;
    user_cmds[EXIT].handler = u_exit;
    user_cmds[USLEEP].handler = u_usleep;
    user_cmds[SLEEP].handler = u_sleep;
    user_cmds[TIME].handler = u_time;

    printf("Welcome to ushell\n");
    // print_env(); /* DEBUG */
    i = 0;
    while (envp[i] != NULL)
    {
        printf("%s\n", envp[i]);
        i++;
    }

    while (1)
    {
        buffer = readline(NULL);
        if (!buffer)
            continue;

        token_count = 0;

        i = 0;
        while (i < len && token_count < MAX_TOKENS)
        {
            tokens[token_count++] = &buffer[i];
            while (buffer[i] != ' ' && buffer[i] != '\0') i++;
            if (buffer[i] == ' ')
            {
                buffer[i] = '\0';
                i++;
                while (buffer[i] == ' ') i++;
            }
        }

        if (tokens[0][0] == 'q')
        {
            break;
        }

        exec_builtin(tokens, token_count);
    }
    _exit(1);
}

void echo(char** argv, int argc)
{
    char* separator = " ";
    char* end = "\n";
    for (int i = 1; i < argc; i++)
    {
        write(1, argv[i], strlen(argv[i]));
        write(1, separator, 1);
    }
    write(1, end, 1);
}

void u_exit(char** argv, int argc)
{
    (void)argv;
    (void)argc;
    exit(0);
}

int atoi(const char* str)
{
    int res = 0;
    int i = 0;
    int sign = 1;

    if (str[0] == '-')
    {
        sign = -1;
        i++;
    }

    for (; str[i] != '\0'; i++)
    {
        res = res * 10 + str[i] - '0';
    }

    return sign * res;
}

void u_sleep(char** argv, int argc)
{
    if (argc < 2)
    {
        printf("Invalid number of arguments for sleep\n");
        return;
    }

    sleep(atoi(argv[1]));
}

void u_usleep(char** argv, int argc)
{
    if (argc < 2)
    {
        printf("Invalid number of arguments for usleep\n");
        return;
    }

    usleep(atoi(argv[1]));
}

void u_time(char** argv, int argc)
{
    (void)argv;
    (void)argc;
    time_t t;
    time(&t);
    printf("Current time: %d\n", t);
}
