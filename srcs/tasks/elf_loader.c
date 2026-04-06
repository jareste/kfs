#include "elf.h"
#include "../memory/memory.h"
#include "../memory/vmm.h"
#include "../memory/pmm.h"
#include "../tasks/task.h"
#include "../utils/utils.h"
#include "../display/display.h"
#include "../ide/ext2_fileio.h"

static int elf_validate(elf32_ehdr_t *hdr)
{
    if (*(uint32_t*)hdr->e_ident != ELF_MAGIC)
    {
        puts_color("ELF: bad magic\n", RED);
        return -1;
    }
    if (hdr->e_machine != EM_386)
    {
        puts_color("ELF: not i386\n", RED);
        return -1;
    }
    if (hdr->e_type != ET_EXEC)
    {
        puts_color("ELF: not executable\n", RED);
        return -1;
    }
    return 0;
}

static uint32_t elf_load(uint8_t *binary)
{
    elf32_ehdr_t *ehdr = (elf32_ehdr_t*)binary;

    if (elf_validate(ehdr) < 0)
        return 0;

    elf32_phdr_t *phdrs = (elf32_phdr_t*)(binary + ehdr->e_phoff);

    for (int i = 0; i < ehdr->e_phnum; i++)
    {
        elf32_phdr_t *ph = &phdrs[i];

        if (ph->p_type != PT_LOAD)
            continue;

        uint32_t vaddr_start = ph->p_vaddr & 0xFFFFF000;
        uint32_t vaddr_end   = PAGE_ALIGN(ph->p_vaddr + ph->p_memsz);
        if (vaddr_end <= vaddr_start)
            vaddr_end = vaddr_start + PAGE_SIZE;

        for (uint32_t va = vaddr_start; va < vaddr_end; va += PAGE_SIZE)
        {
            if (vmm_get_physical(vmm_current_directory(), va) != 0)
                continue;

            uint32_t pa = pmm_alloc_frame();
            vmm_map_page(vmm_current_directory(), va, pa, PAGE_USER_RW);

            uint32_t *dir = (uint32_t*)vmm_current_directory();
            dir[va >> 22] |= PAGE_USER;

            __asm__ __volatile__("invlpg (%0)" : : "r"(va) : "memory");
            __asm__ __volatile__("invlpg (%0)" : : "r"(&dir[va >> 22]) : "memory");
        }


        memcpy((void*)ph->p_vaddr, binary + ph->p_offset, ph->p_filesz);

        /* BSS */
        if (ph->p_memsz > ph->p_filesz)
            memset((void*)(ph->p_vaddr + ph->p_filesz), 0,
                   ph->p_memsz - ph->p_filesz);
    }

    /* I think it's not needed. */
    __asm__ __volatile__(
        "mov %%cr3, %%eax\n"
        "mov %%eax, %%cr3\n"
        : : : "eax", "memory"
    );

    return ehdr->e_entry;
}

void exec_bin(const char* path)
{
    uint32_t entry;
    ssize_t file_size;
    uint8_t *binary;
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

    entry = elf_load(binary);
    if (entry == 0)
    {
        puts_color("ELF load failed\n", RED);
        kfree(binary);
        return;
    }

    create_user_task_at(entry, path, NULL);

    kfree(binary);

    return ;
}
