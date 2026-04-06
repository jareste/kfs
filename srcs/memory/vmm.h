#ifndef VMM_H
#define VMM_H

#include "../utils/stdint.h"

/* ------------------------------------------------------------------ */
/*  Page size and address decomposition                               */
/* ------------------------------------------------------------------ */

#define PAGE_SIZE           4096u
#define PAGE_ALIGN(a)       (((a) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))

/*
 * A 32-bit linear (virtual) address is split as:
 *
 *  31        22 | 21       12 | 11          0
 *  [ dir idx  ] [ table idx ] [   offset   ]
 *     10 bits       10 bits       12 bits
 */
#define VMM_DIR_INDEX(va)    (((va) >> 22) & 0x3FF)
#define VMM_TABLE_INDEX(va)  (((va) >> 12) & 0x3FF)
#define VMM_OFFSET(va)       ((va) & 0xFFF)

/* ------------------------------------------------------------------ */
/*  Page directory / page table entry flags                           */
/* ------------------------------------------------------------------ */

#define PAGE_PRESENT    (1u << 0)   /* P   – page is in physical memory    */
#define PAGE_RW         (1u << 1)   /* R/W – 0 = read-only, 1 = read/write */
#define PAGE_USER       (1u << 2)   /* U/S – 0 = kernel only, 1 = user ok  */
#define PAGE_ACCESSED   (1u << 5)   /* A   – set by CPU on read            */
#define PAGE_DIRTY      (1u << 6)   /* D   – set by CPU on write (PTE only)*/

/* Convenience combinations */
#define PAGE_KERNEL_RW  (PAGE_PRESENT | PAGE_RW)
#define PAGE_KERNEL_RO  (PAGE_PRESENT)
#define PAGE_USER_RW    (PAGE_PRESENT | PAGE_RW  | PAGE_USER)
#define PAGE_USER_RO    (PAGE_PRESENT | PAGE_USER)

/* Mask to extract the physical frame address from an entry */
#define PAGE_FRAME_MASK 0xFFFFF000u

/* ------------------------------------------------------------------ */
/*  Structures                                                         */
/* ------------------------------------------------------------------ */

/*
 * A page table holds 1024 entries of 4 bytes each = 4 KB.
 * Each entry stores the physical address of a 4 KB page frame
 * (bits 31:12) plus flags (bits 11:0).
 */
typedef struct __attribute__((aligned(4096))) {
    uint32_t entries[1024];
} page_table_t;

/*
 * A page directory holds 1024 entries of 4 bytes each = 4 KB.
 * Each entry stores the physical address of a page table (bits 31:12)
 * plus flags (bits 11:0).
 */
typedef struct __attribute__((aligned(4096))) {
    uint32_t entries[1024];
} page_directory_t;

/* ------------------------------------------------------------------ */
/*  Kernel / user space boundaries                                    */
/* ------------------------------------------------------------------ */

/*
 * We split the 4 GB virtual address space in two halves:
 *
 *   0x00000000 – 0xBFFFFFFF  →  User space   (3 GB)
 *   0xC0000000 – 0xFFFFFFFF  →  Kernel space (1 GB)
 *
 * This is the classic Linux-style split.  All kernel code/data is
 * identity-mapped into the lower physical addresses but the boundary
 * is enforced via the U/S flag in page entries.
 */
#define KERNEL_VIRTUAL_BASE 0xC0000000u
#define USER_SPACE_END      0xBFFFFFFFu

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */

/*
 * vmm_init - set up the kernel page directory and enable paging.
 *
 * Identity-maps the first 16 MB (more than enough for the kernel + PMM
 * bitmap) as kernel-only read/write pages, then loads CR3 and sets
 * bit 31 of CR0.  After this call, virtual == physical for all
 * currently used addresses.
 */
void vmm_init(void);

/*
 * vmm_map_page - map a single virtual page to a physical frame.
 *
 * @dir   – page directory to operate on
 * @virt  – virtual address (will be rounded down to page boundary)
 * @phys  – physical address of frame (must be page-aligned)
 * @flags – combination of PAGE_* flags
 *
 * Allocates a new page table via pmm_alloc_frame() if the directory
 * entry is not yet present.
 */
void vmm_map_page(page_directory_t *dir,
                  uint32_t virt, uint32_t phys, uint32_t flags);

/*
 * vmm_unmap_page - remove a virtual→physical mapping.
 *
 * Does NOT free the physical frame; the caller is responsible for
 * calling pmm_free_frame() if the frame should be reclaimed.
 * Invalidates the TLB entry for the page via INVLPG.
 */
void vmm_unmap_page(page_directory_t *dir, uint32_t virt);

/*
 * vmm_get_physical - translate a virtual address to physical.
 *
 * Returns 0 if the page is not mapped.
 */
uint32_t vmm_get_physical(page_directory_t *dir, uint32_t virt);

/*
 * vmm_switch_directory - load a page directory into CR3.
 *
 * Use this when switching between processes (not needed for KFS3
 * mandatory part, but useful to have).
 */
void vmm_switch_directory(page_directory_t *dir);

/*
 * vmm_current_directory - return the currently active page directory.
 */
page_directory_t *vmm_current_directory(void);

/*
 * vmm_alloc_page - allocate a physical frame and map it at @virt.
 *
 * Convenience wrapper: calls pmm_alloc_frame() then vmm_map_page().
 * Returns the physical address of the allocated frame.
 */
uint32_t vmm_alloc_page(page_directory_t *dir, uint32_t virt, uint32_t flags);

/*
 * vmm_free_page - unmap @virt and free its backing physical frame.
 */
void vmm_free_page(page_directory_t *dir, uint32_t virt);

#endif /* VMM_H */
