#include "vmm.h"
#include "pmm.h"
#include "../utils/utils.h"
#include "../panic/kpanic.h"
#include "../display/display.h"

/* ------------------------------------------------------------------ */
/*  Linker symbols                                                     */
/* ------------------------------------------------------------------ */

extern uint32_t _kernel_end;   /* first byte past the kernel image     */

/* ------------------------------------------------------------------ */
/*  Module state                                                       */
/* ------------------------------------------------------------------ */

/*
 * The kernel page directory.  Declared as a global so its physical
 * address (= virtual address before higher-half mapping) is known at
 * compile time and is page-aligned thanks to the struct attribute.
 *
 * We allocate it statically here; once paging is on this region is
 * already identity-mapped so the CPU can reach it.
 */
static page_directory_t kernel_directory __attribute__((aligned(4096)));

/* Pointer to the directory currently loaded in CR3 */
static page_directory_t *current_dir = 0;

/* assembly inline helpers */
static inline void tlb_flush_page(uint32_t virt)
{
    __asm__ __volatile__("invlpg (%0)" : : "r"(virt) : "memory");
}

/* Reload CR3 to flush the entire TLB */
static inline void tlb_flush_all(void)
{
    uint32_t cr3;
    __asm__ __volatile__(
        "mov %%cr3, %0\n"
        "mov %0, %%cr3\n"
        : "=r"(cr3) : : "memory"
    );
}

/*
 * Get (or create) the page table for directory entry @dir_idx.
 *
 * If the entry is not present, a new page table is allocated from the
 * PMM and zeroed.  The directory entry is updated and the new table's
 * virtual address is returned.
 */
static page_table_t *get_or_create_table(page_directory_t *dir, uint32_t dir_idx, uint32_t flags)
{
    page_table_t *table;
    int32_t i;
    uint32_t phys;
    uint32_t entry = dir->entries[dir_idx];

    if (entry & PAGE_PRESENT)
    {
        /* Table already exists – return its virtual (= physical here) addr */
        return (page_table_t *)(entry & PAGE_FRAME_MASK);
    }

    /* Allocate a fresh physical frame for the new page table */
    phys = pmm_alloc_frame();
    if (phys == 0)
        kpanic("VMM: pmm_alloc_frame returned 0 for page table", 1);

    /* Zero out the new table */
    table = (page_table_t *)phys;
    memset(table, 0, sizeof(page_table_t));

    /* Install into the directory.  Always allow kernel at least R/W. */
    dir->entries[dir_idx] = phys | PAGE_PRESENT | PAGE_RW | (flags & PAGE_USER);

    return table;
}

void vmm_map_page(page_directory_t *dir, uint32_t virt, uint32_t phys, uint32_t flags)
{
    virt &= PAGE_FRAME_MASK; /* align down */
    phys &= PAGE_FRAME_MASK;

    uint32_t    di    = VMM_DIR_INDEX(virt);
    uint32_t    ti    = VMM_TABLE_INDEX(virt);
    page_table_t *tbl = get_or_create_table(dir, di, flags);

    if (tbl->entries[ti] & PAGE_PRESENT)
        kpanic("VMM: vmm_map_page - page already mapped", 1);

    if (flags & PAGE_USER)
        dir->entries[di] |= PAGE_USER;

    tbl->entries[ti] = phys | (flags & 0xFFF) | PAGE_PRESENT;

    /* Invalidate the TLB for this virtual address */
    if (current_dir == dir)
        tlb_flush_page(virt);
    __asm__ __volatile__(
        "mov %%cr3, %%eax\n"
        "mov %%eax, %%cr3\n"
        : : : "eax", "memory"
    );
}

void vmm_unmap_page(page_directory_t *dir, uint32_t virt)
{
    virt &= PAGE_FRAME_MASK;

    uint32_t di    = VMM_DIR_INDEX(virt);
    uint32_t ti    = VMM_TABLE_INDEX(virt);
    uint32_t entry = dir->entries[di];

    if (!(entry & PAGE_PRESENT))
        return; /* directory entry not present – nothing to do */

    page_table_t *tbl = (page_table_t *)(entry & PAGE_FRAME_MASK);

    if (!(tbl->entries[ti] & PAGE_PRESENT))
        return; /* page not mapped */

    tbl->entries[ti] = 0;

    if (current_dir == dir)
        tlb_flush_page(virt);
}

uint32_t vmm_get_physical(page_directory_t *dir, uint32_t virt)
{
    uint32_t di    = VMM_DIR_INDEX(virt);
    uint32_t ti    = VMM_TABLE_INDEX(virt);
    uint32_t entry = dir->entries[di];

    if (!(entry & PAGE_PRESENT))
        return 0;

    page_table_t *tbl = (page_table_t *)(entry & PAGE_FRAME_MASK);

    if (!(tbl->entries[ti] & PAGE_PRESENT))
        return 0;

    return (tbl->entries[ti] & PAGE_FRAME_MASK) + VMM_OFFSET(virt);
}

void vmm_set_kernel_dir(void)
{
    current_dir = &kernel_directory;
     __asm__ __volatile__(
        "mov %0, %%cr3"
        : : "r"((uint32_t)&kernel_directory) : "memory"
    );
}

void vmm_switch_directory(page_directory_t *dir)
{
    current_dir = dir;
    __asm__ __volatile__(
        "mov %0, %%cr3"
        : : "r"((uint32_t)dir) : "memory"
    );
}

page_directory_t *vmm_current_directory(void)
{
    return current_dir;
}

page_directory_t* vmm_clone_directory(page_directory_t *src)
{
    uint32_t phys = pmm_alloc_frame();
    page_directory_t *dir = (page_directory_t*)phys;
    memset(dir, 0, sizeof(page_directory_t));

    for (int32_t i = 0; i < 1024; i++)
    {
        if (!src->entries[i])
            continue;

        uint32_t virt_start = i * 4 * 1024 * 1024;

        if (virt_start >= 0x08000000 && virt_start < 0x0C000000)
            continue;

        dir->entries[i] = src->entries[i];
    }

    return dir;
}

uint32_t vmm_alloc_page(page_directory_t *dir, uint32_t virt, uint32_t flags)
{
    uint32_t phys = pmm_alloc_frame();
    if (phys == 0)
        kpanic("VMM: vmm_alloc_page - out of physical memory", 1);

    vmm_map_page(dir, virt, phys, flags);
    return phys;
}

void vmm_free_page(page_directory_t *dir, uint32_t virt)
{
    uint32_t phys = vmm_get_physical(dir, virt);
    if (phys == 0)
        kpanic("VMM: vmm_free_page - page not mapped", 0);

    vmm_unmap_page(dir, virt);
    pmm_free_frame(phys & PAGE_FRAME_MASK);
}

/* ------------------------------------------------------------------ */
/*  vmm_init – identity-map kernel memory and enable paging           */
/* ------------------------------------------------------------------ */

// void vmm_init(void)
// {
//     for (int32_t i = 0; i < 1024; i++)
//         kernel_directory.entries[i] = 0;

//     /*
//      * Identity-map the first 16 MB as kernel read/write.
//      */
//     uint32_t identity_end = 16 * 1024 * 1024; /* 16 MB */

//     for (uint32_t phys = 0; phys < identity_end; phys += PAGE_SIZE) {
//         vmm_map_page(&kernel_directory, phys, phys, PAGE_KERNEL_RW);
//     }

//     uint32_t kernel_top = PAGE_ALIGN((uint32_t)&_kernel_end);
//     if (kernel_top > identity_end) {
//         for (uint32_t a = identity_end; a < kernel_top; a += PAGE_SIZE)
//             vmm_map_page(&kernel_directory, a, a, PAGE_KERNEL_RW);
//     }

//     kprintf("[VMM] identity-mapped 0x0 - 0x%x\n",
//            (kernel_top > identity_end ? kernel_top : identity_end));

//     /*
//      * Load CR3 with the kernel directory's physical address.
//      * Enable paging by setting bit 31 of CR0.
//      *
//      * After this point every memory access goes through the MMU.
//      * Because we identity-mapped everything in use, execution continues
//      * without any faults.
//      */
//     current_dir = &kernel_directory;

//     __asm__ __volatile__(
//         /* Load page directory */
//         "mov %0, %%cr3\n"
//         /* Set PG bit (bit 31) in CR0 – enable paging */
//         "mov %%cr0, %%eax\n"
//         "or  $0x80000000, %%eax\n"
//         "mov %%eax, %%cr0\n"
//         : : "r"((uint32_t)&kernel_directory) : "eax", "memory"
//     );

//     kprintf("[VMM] paging enabled\n");
// }

void vmm_init(void)
{
    /*
     * Clear the kernel page directory.
     */
    for (int i = 0; i < 1024; i++)
        kernel_directory.entries[i] = 0;
 
    /*
     * Identity-map ALL physical RAM (virtual address == physical address).
     *
     * Why map everything and not just 16 MB?
     *
     * The PMM hands out physical frames from anywhere in RAM.  When
     * vmm_map_page() needs a new page table it calls pmm_alloc_frame()
     * and zeroes the returned frame by writing to its physical address
     * directly (get_or_create_table does `page_table_t *table =
     * (page_table_t *)phys`).  If that physical address is above the
     * identity-mapped ceiling the write faults immediately — which is
     * exactly what happened at 0x01229000 when allocating >16 MB via
     * vmalloc (the new page table frame landed above 16 MB).
     *
     * Mapping all of physical RAM up-front is the standard solution
     * (Linux calls this the "direct map").  It costs one page-directory
     * entry per 4 MB of RAM — negligible.
     *
     * We derive the ceiling from the PMM so we never map more than what
     * the hardware actually has.
     */
    uint32_t total_phys_bytes = pmm_total_frames() * PAGE_SIZE;
 
    /*
     * Safety cap: never identity-map above 0xC0000000 (3 GB).
     * On a 32-bit kernel we reserve the top gigabyte for kernel virtual
     * structures.  In practice QEMU gives <=128 MB so this is never hit.
     */
    if (total_phys_bytes > 0xC0000000u)
        total_phys_bytes = 0xC0000000u;
 
    for (uint32_t phys = 0; phys < total_phys_bytes; phys += PAGE_SIZE)
        vmm_map_page(&kernel_directory, phys, phys, PAGE_KERNEL_RW);
 
    kprintf("[VMM] identity-mapped 0x0 - %x (%d MB)\n",
           total_phys_bytes, total_phys_bytes / (1024 * 1024));
 
    /*
     * Load CR3 with the kernel directory's physical address.
     * Enable paging by setting bit 31 of CR0.
     *
     * After this point every memory access goes through the MMU.
     * Because we identity-mapped everything in use, execution continues
     * without any faults.
     */
    current_dir = &kernel_directory;
 
    __asm__ __volatile__(
        /* Load page directory */
        "mov %0, %%cr3\n"
        /* Set PG bit (bit 31) in CR0 – enable paging */
        "mov %%cr0, %%eax\n"
        "or  $0x80000000, %%eax\n"
        "mov %%eax, %%cr0\n"
        : : "r"((uint32_t)&kernel_directory) : "eax", "memory"
    );
 
    kprintf("[VMM] paging enabled\n");
}