#ifndef ENV_H
#define ENV_H

typedef struct env_entry
{
    char *key;
    char *value;
    struct env_entry *next;
} env_entry_t;

typedef struct env_hashtable
{
    size_t size;
    env_entry_t **buckets;
} env_hashtable_t;


char* _getenv(const char *name);

int _setenv(const char *name, const char *value, int overwrite);

int _unsetenv(const char *name);

int _putenv(char *string);

char** _get_full_env(void);

void set_active_env(env_hashtable_t *env);

void env_hashtable_destroy(env_hashtable_t *table);

void print_env(void);

#endif
