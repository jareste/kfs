#include "../utils/utils.h"
#include "../utils/stdint.h"
#include "../memory/memory.h"
#include "env.h"

#define INITIAL_HASHTABLE_SIZE 128

env_hashtable_t *active_env = NULL;

/* djb2 hash function (by Dan Bernstein) */
unsigned long hash_string(const char *str)
{
    unsigned long hash = 5381;
    int c;

    while ((c = *str++))
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */

    return hash;
}

env_hashtable_t* env_hashtable_create(size_t size)
{
    env_hashtable_t *table = vmalloc(sizeof(env_hashtable_t));
    if (!table) return NULL;
    table->size = size;
    table->buckets = vmalloc(size * sizeof(env_entry_t*));
    memset(table->buckets, 0, size * sizeof(env_entry_t*));
    if (!table->buckets)
    {
        vfree(table);
        return NULL;
    }
    return table;
}

void env_hashtable_destroy(env_hashtable_t *table)
{
    if (!table) return;
    for (size_t i = 0; i < table->size; i++)
    {
        env_entry_t *entry = table->buckets[i];
        while (entry)
        {
            env_entry_t *next = entry->next;
            vfree(entry->key);
            vfree(entry->value);
            vfree(entry);
            entry = next;
        }
    }
    vfree(table->buckets);
    vfree(table);
}

int env_hashtable_set(env_hashtable_t *table, const char *key, const char *value)
{
    if (!table || !key || !value) return -1;
    unsigned long hash = hash_string(key);
    size_t index = hash % table->size;

    env_entry_t *entry = table->buckets[index];
    while (entry)
    {
        if (strcmp(entry->key, key) == 0)
        {
            char *new_value = vstrdup(value);
            if (!new_value) return -1;
            vfree(entry->value);
            entry->value = new_value;
            return 0;
        }
        entry = entry->next;
    }
    
    env_entry_t *new_entry = vmalloc(sizeof(env_entry_t));
    if (!new_entry) return -1;
    new_entry->key = vstrdup(key);
    new_entry->value = vstrdup(value);
    if (!new_entry->key || !new_entry->value)
    {
        vfree(new_entry->key);
        vfree(new_entry->value);
        vfree(new_entry);
        return -1;
    }
    new_entry->next = table->buckets[index];
    table->buckets[index] = new_entry;
    return 0;
}

char* env_hashtable_get(env_hashtable_t *table, const char *key)
{
    if (!table || !key) return NULL;
    unsigned long hash = hash_string(key);
    size_t index = hash % table->size;
    env_entry_t *entry = table->buckets[index];
    while (entry)
    {
        if (strcmp(entry->key, key) == 0)
            return entry->value;
        entry = entry->next;
    }
    return NULL;
}

int env_hashtable_remove(env_hashtable_t *table, const char *key)
{
    if (!table || !key) return -1;
    unsigned long hash = hash_string(key);
    size_t index = hash % table->size;
    env_entry_t *entry = table->buckets[index];
    env_entry_t *prev = NULL;
    while (entry)
    {
        if (strcmp(entry->key, key) == 0)
        {
            if (prev)
                prev->next = entry->next;
            else
                table->buckets[index] = entry->next;
            vfree(entry->key);
            vfree(entry->value);
            vfree(entry);
            return 0;
        }
        prev = entry;
        entry = entry->next;
    }
    return -1;
}


char** get_full_env(env_hashtable_t *table)
{
    if (!table)
        return NULL;
    
    size_t count = 0;
    for (size_t i = 0; i < table->size; i++)
    {
        env_entry_t *entry = table->buckets[i];
        while (entry)
        {
            count++;
            entry = entry->next;
        }
    }
    
    char **envp = vmalloc((count + 1) * sizeof(char*));
    if (!envp)
        return NULL;
    
    size_t idx = 0;
    for (size_t i = 0; i < table->size; i++)
    {
        env_entry_t *entry = table->buckets[i];
        while (entry)
        {
            size_t key_len   = strlen(entry->key);
            size_t value_len = strlen(entry->value);
            size_t total_len = key_len + 1 + value_len + 1;
            char *env_str = vmalloc(total_len);
            if (!env_str)
            {
                for (size_t j = 0; j < idx; j++)
                {
                    vfree(envp[j]);
                }
                vfree(envp);
                return NULL;
            }
            memcpy(env_str, entry->key, key_len);
            env_str[key_len] = '=';
            memcpy(env_str + key_len + 1, entry->value, value_len);
            env_str[total_len - 1] = '\0';
            envp[idx++] = env_str;
            entry = entry->next;
        }
    }
    
    envp[idx] = NULL;
    return envp;
}

char** _get_full_env(void)
{
    return get_full_env(active_env);
}

char* _getenv(const char *name)
{
    return env_hashtable_get(active_env, name);
}

int _setenv(const char *name, const char *value, int overwrite)
{
    if (!active_env || !name || !value)
        return -1;
    if (!overwrite && _getenv(name))
        return 0;
    return env_hashtable_set(active_env, name, value);
}

int _unsetenv(const char *name)
{
    if (!active_env)
        return -1;
    return env_hashtable_remove(active_env, name);
}

int _putenv(char *string)
{
    /* Expect string in "KEY=value" format. */
    char *eq = strchr(string, '=');
    if (!eq)
        return -1;
    
    /* Temporarily split the string to isolate key and value */
    *eq = '\0';
    int result = _setenv(string, eq + 1, 1);
    *eq = '='; /* Restore the '=' character */
    return result;
}

void print_env(void)
{
    if (!active_env)
        return;
    
    for (size_t i = 0; i < active_env->size; i++)
    {
        env_entry_t *entry = active_env->buckets[i];
        while (entry)
        {
            kprintf("%s=%s\n", entry->key, entry->value);
            entry = entry->next;
        }
    }
}

void set_active_env(env_hashtable_t *env)
{
    active_env = env;
}
