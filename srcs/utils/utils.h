#ifndef UTILS_H
#define UTILS_H

#include "stdint.h"
#define NULL (void*)0
#define UNUSED(x); (void)(x);
#define MB(x) ((x) * 1024 * 1024) /* Convert MB to bytes */
#define KB(x) (x * 1024) /* Convert KB to bytes */

typedef enum bool {
    false = 0,
    true = 1
} bool;

typedef struct {
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    uint32_t esi;
    uint32_t edi;
    uint32_t ebp;
    uint32_t esp;
    uint32_t eip;
    uint32_t eflags;
    uint32_t cs;
    uint32_t ds;
    uint32_t es;
    uint32_t fs;
    uint32_t gs;
    uint32_t ss;
} registers_t;

int strlen(const char *str);
int strcmp(const char *str1, const char *str2);
void itoa(int value, char *str, int base);
void memset(void *dest, uint8_t val, uint32_t len);
int strncmp (const char *s1, const char *s2, size_t n);
uint32_t strtol(const char* str, char** endptr, int base);
uint32_t hex_string_to_int(const char *hex_str);
uint32_t get_stack_pointer();
void dump_registers_c(registers_t* regs);
void kernel_panic_(char* msg, const char* file, int line, const char* func_name);
#define kernel_panic(msg) kernel_panic_(msg, __FILE__, __LINE__, __func__)
extern void dump_registers();
void *memcpy(void *dest, const void *src, size_t n);
int memcmp(const void* s1, const void* s2, size_t n);
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, size_t n);
char *strncat(char *dest, const char *src, size_t n);
char *strrchr(const char *s, int c);
char *strtok(char *str, const char *delim);
size_t strcspn(const char *s, const char *reject);
size_t strspn(const char *s, const char *accept);
char *strcat(char *dest, const char *src);
char *strchr(const char *s, int c);
void *memmove(void *dest, const void *src, size_t n);
void uitoa(uint32_t num, char *buf);

#define ASSERT(x)   if (!(x)) { kernel_panic("Assertion failed: " #x); }
#define NEVER_HERE  kernel_panic("NEVER_HERE")

#endif
