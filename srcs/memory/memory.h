#ifndef MEMORY_H
#define MEMORY_H

#include "../utils/stdint.h"

typedef int off_t;

#define PAGE_DIRECTORY_ENTRIES  1024
#define PAGE_TABLE_ENTRIES      1024
#define PAGE_SIZE               0x1000  /* 4 KB */
#define PAGE_PRESENT            0x1
#define PAGE_WRITE              0x2
#define PAGE_RW                 0x2
#define PAGE_USER               0x4

#define PROT_READ               0x1
#define PROT_WRITE              0x2
#define PROT_EXEC               0x4

#define PROT_USER               0x8

#define MAP_ANONYMOUS           0x20
#define MAP_FIXED               0x40

#include "kmalloc.h"
#include "vmalloc.h"

#endif // MEMORY_H
