#include "modules.h"
#include "module_exports.h"
#include "../tasks/elf.h"
#include "elf_module.h"
#include "../ide/ext2_fileio.h"
#include "../display/display.h"
#include "../memory/memory.h"

EXPORT_SYMBOL(kmalloc);
EXPORT_SYMBOL(kfree);
EXPORT_SYMBOL(kprintf);
EXPORT_SYMBOL(register_module);
EXPORT_SYMBOL(unregister_module);

extern kernel_symbol_t __ksymtab_start[];
extern kernel_symbol_t __ksymtab_end[];

static void *ksym_lookup(const char *name)
{
    kernel_symbol_t *sym;

    for (sym = __ksymtab_start; sym < __ksymtab_end; sym++)
        if (strcmp(sym->name, name) == 0)
            return sym->addr;
    return NULL;
}

/* Loads and registers a module */
/* TODO fix this messy function that somehow works */
module_t *elf_load_module(uint8_t *binary)
{
    elf32_ehdr_t *ehdr = (elf32_ehdr_t *)binary;

    /* Validate it's relocable elf object */
    if (*(uint32_t*)ehdr->e_ident != ELF_MAGIC || ehdr->e_type != ET_REL)
    {
        kprintf("elf_load_module: not a relocatable ELF\n");
        return NULL;
    }

    elf32_shdr_t *shdrs = (elf32_shdr_t *)(binary + ehdr->e_shoff);
    char *shstrtab = (char *)(binary + shdrs[ehdr->e_shstrndx].sh_offset);

    /* Assign memory for each section and remember its address at runtime */
    uint32_t *section_addrs = module_alloc(ehdr->e_shnum * sizeof(uint32_t));
    memset(section_addrs, 0, ehdr->e_shnum * sizeof(uint32_t));

    for (int i = 0; i < ehdr->e_shnum; i++)
    {
        elf32_shdr_t *sh = &shdrs[i];
        if (sh->sh_size == 0 || !(sh->sh_flags & 0x2 /* SHF_ALLOC */))
            continue;

        uint8_t *mem = module_alloc(sh->sh_size);
        if (sh->sh_type == 8 /* SHT_NOBITS, BSS */)
            memset(mem, 0, sh->sh_size);
        else
            memcpy(mem, binary + sh->sh_offset, sh->sh_size);

        section_addrs[i] = (uint32_t)mem;
    }

    /* Search for symbol and string tables */
    elf32_sym_t *symtab = NULL;
    char        *strtab = NULL;
    int          sym_count = 0;

    for (int i = 0; i < ehdr->e_shnum; i++)
    {
        elf32_shdr_t *sh = &shdrs[i];
        if (sh->sh_type == SHT_SYMTAB)
        {
            symtab    = (elf32_sym_t *)(binary + sh->sh_offset);
            sym_count = sh->sh_size / sizeof(elf32_sym_t);
            strtab    = (char *)(binary + shdrs[sh->sh_link].sh_offset);
        }
    }

    (void)sym_count; /* TODO use it for something, maybe debug print all symbols? */
    if (!symtab)
    {
        kprintf("elf_load_module: no symtab\n");
        return NULL;
    }

    /* Apply relocations */
    for (int i = 0; i < ehdr->e_shnum; i++)
    {
        elf32_shdr_t *sh = &shdrs[i];
        if (sh->sh_type != SHT_REL)
            continue;

        /* sh_info = where to apply the relocations */
        uint32_t target_addr = section_addrs[sh->sh_info];
        if (!target_addr)
            continue;

        elf32_rel_t *rels = (elf32_rel_t *)(binary + sh->sh_offset);
        int rel_count = sh->sh_size / sizeof(elf32_rel_t);

        for (int j = 0; j < rel_count; j++)
        {
            elf32_rel_t *rel = &rels[j];
            uint32_t sym_idx = ELF32_R_SYM(rel->r_info);
            uint8_t  rel_type = ELF32_R_TYPE(rel->r_info);

            elf32_sym_t *sym = &symtab[sym_idx];

            /* Look for the symbol. basically find where it lives */
            uint32_t S = 0;
            if (sym->st_shndx == SHN_UNDEF)
            {
                /* External symbol, look if we got it */
                const char *sym_name = strtab + sym->st_name;
                S = (uint32_t)ksym_lookup(sym_name);
                if (!S)
                {
                    kprintf("elf_load_module: unresolved symbol '%s'\n", sym_name);
                    return NULL;
                }
            }
            else
            {
                /* Local symbol */
                S = section_addrs[sym->st_shndx] + sym->st_value;
            }

            /* Where to patch */
            uint32_t P = target_addr + rel->r_offset;
            uint32_t *patch = (uint32_t *)P;

            /* addend (value that is already in the place to patch) */
            int32_t A = (int32_t)*patch;

            switch (rel_type)
            {
                case R_386_32:
                    *patch = S + A;
                    break;
                case R_386_PC32:
                    *patch = S + A - P;
                    break;
                default:
                    kprintf("elf_load_module: unknown reloc type %d\n", rel_type);
                    return NULL;
            }
        }
    }

    module_init_fn_t init_fn = NULL;
    module_exit_fn_t exit_fn = NULL;

    for (int i = 0; i < ehdr->e_shnum; i++)
    {
        const char *sname = shstrtab + shdrs[i].sh_name;
        
        if (strcmp(sname, ".mod_init") == 0 && section_addrs[i])
            init_fn = *(module_init_fn_t *)section_addrs[i];
        
        if (strcmp(sname, ".mod_exit") == 0 && section_addrs[i])
            exit_fn = *(module_exit_fn_t *)section_addrs[i];
    }

    if (!init_fn)
    {
        kprintf("elf_load_module: no MODULE_INIT defined\n");
        return NULL;
    }

    int ret = init_fn();
    if (ret < 0)
    {
        kprintf("elf_load_module: init failed (%d)\n", ret);
        return NULL;
    }

    (void)exit_fn; /* TODO use it on rmmod */
    return 0x1; /* TODO change function return to int instead of module_t* */
}

void insmod(const char *path)
{
    ssize_t file_size;
    uint8_t *binary;
    module_t *mod;
    int fd;
    
    fd = sys_open(path, O_RDONLY);
    if (fd < 0)
    {
        puts_color("Failed to open binary\n", RED);
        return;
    }
    file_size = sys_lseek(fd, 0, SEEK_END);
    if (file_size < 0)
    {
        puts_color("Failed to get file size\n", RED);
        sys_close(fd);
        return;
    }
    sys_lseek(fd, 0, SEEK_SET);
    binary = kmalloc(file_size);
    if (sys_read(fd, binary, file_size) != file_size)
    {
        puts_color("Failed to read binary\n", RED);
        kfree(binary);
        sys_close(fd);
        return;
    }
    sys_close(fd);

    mod = elf_load_module(binary);
    if (!mod)
    {
        kprintf("insmod: failed\n");
        return;
    }
    kprintf("Module loaded successfully\n");
}
