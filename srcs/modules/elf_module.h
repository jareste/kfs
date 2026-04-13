#ifndef ELF_MODULE_H
#define ELF_MODULE_H

typedef struct
{
    uint32_t sh_name;
    uint32_t sh_type;
    uint32_t sh_flags;
    uint32_t sh_addr;
    uint32_t sh_offset;
    uint32_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint32_t sh_addralign;
    uint32_t sh_entsize;
} elf32_shdr_t;

typedef struct
{
    uint32_t st_name;
    uint32_t st_value;
    uint32_t st_size;
    uint8_t  st_info;
    uint8_t  st_other;
    uint16_t st_shndx;
} elf32_sym_t;

typedef struct
{
    uint32_t r_offset;
    uint32_t r_info;
} elf32_rel_t;

#define ELF32_R_SYM(i)  ((i) >> 8)
#define ELF32_R_TYPE(i) ((uint8_t)(i))

#define R_386_32    1 /* S + A      (absolute)*/
#define R_386_PC32  2 /* S + A - P  (relative to PC) */

#define SHT_REL     9
#define SHT_SYMTAB  2
#define SHT_STRTAB  3
#define SHN_UNDEF   0

#endif /* ELF_MODULE_H */
