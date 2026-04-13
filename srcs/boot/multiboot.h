#ifndef MULTIBOOT_H
#define MULTIBOOT_H

#include "../utils/stdint.h"

/* Copied from GRUB documentation */

/* Multiboot magic passed by GRUB in EAX */
#define MULTIBOOT_MAGIC         0x2BADB002

/* Flags in multiboot_info_t.flags */
#define MULTIBOOT_FLAG_MEM      (1 << 0)   /* mem_lower / mem_upper valid */
#define MULTIBOOT_FLAG_MMAP     (1 << 6)   /* mmap_* fields valid         */

/* Values for multiboot_mmap_entry_t.type */
#define MULTIBOOT_MEMORY_AVAILABLE  1
#define MULTIBOOT_MEMORY_RESERVED   2

/* ------------------------------------------------------------------ */
/*  Memory-map entry (each entry is preceded by its own size field)   */
/* ------------------------------------------------------------------ */
typedef struct __attribute__((packed)) {
    uint32_t size;      /* size of this entry (not counting this field) */
    uint32_t addr_low;
    uint32_t addr_high;
    uint32_t len_low;
    uint32_t len_high;
    uint32_t type;      /* 1 = available, anything else = reserved      */
} multiboot_mmap_entry_t;

/* ------------------------------------------------------------------ */
/*  Main multiboot information structure passed by GRUB in EBX        */
/* ------------------------------------------------------------------ */
typedef struct __attribute__((packed)) {
    uint32_t flags;         /* which fields below are valid             */

    /* flags[0]: basic memory info */
    uint32_t mem_lower;     /* KB below 1 MB  (usually 640)            */
    uint32_t mem_upper;     /* KB above 1 MB                           */

    /* flags[1]: boot device */
    uint32_t boot_device;

    /* flags[2]: command line */
    uint32_t cmdline;

    /* flags[3]: modules */
    uint32_t mods_count;
    uint32_t mods_addr;

    /* flags[4,5]: symbol table (a.out / ELF) */
    uint32_t syms[4];

    /* flags[6]: memory map */
    uint32_t mmap_length;   /* total byte-length of the mmap buffer    */
    uint32_t mmap_addr;     /* physical address of first mmap entry    */

    /* remaining fields */
    uint32_t drives_length;
    uint32_t drives_addr;
    uint32_t config_table;
    uint32_t boot_loader_name;
    uint32_t apm_table;
} multiboot_info_t;

#endif /* MULTIBOOT_H */
