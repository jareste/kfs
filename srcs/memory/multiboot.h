// multiboot.h
#ifndef MULTIBOOT_H
#define MULTIBOOT_H

#include <stdint.h>

#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002

/* The tag for a single mmap entry. Note that Multiboot2 adds size at front;
   Multiboot1 uses a different format, but GRUB multiboot1 with flag 3 uses E820. */
typedef struct {
    uint32_t size;    // should be 20
    uint64_t addr;
    uint64_t len;
    uint32_t type;    // 1 = RAM, other = reserved
} __attribute__((packed)) mmap_entry_t;

typedef struct {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    /* Syms for a.out or ELF */
    union {
      struct {
        uint32_t tabsize, strsize, addr, reserved;
      } aout;
      struct {
        uint32_t num, size, addr, shndx;
      } elf;
    } syms;
    uint32_t mmap_length;
    uint32_t mmap_addr;
    // ... (we only care up to here)
} __attribute__((packed)) multiboot_info_t;

#endif // MULTIBOOT_H
