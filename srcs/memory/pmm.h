#ifndef PMM_H
#define PMM_H

#include "../utils/stdint.h"
#include "../boot/multiboot.h"

/* ------------------------------------------------------------------ */
/*  Constants                                                          */
/* ------------------------------------------------------------------ */

#ifndef PAGE_SIZE
#define PAGE_SIZE       4096            /* 4 KB pages                  */
#endif
#ifndef PAGE_ALIGN
#define PAGE_ALIGN(a)   (((a) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))
#endif

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

/*
 * pmm_init - initialise the physical memory manager.
 *
 * Reads the GRUB memory map from @mbi, builds a bitmap of all physical
 * frames, marks everything used, then marks available regions free.
 * Finally it marks the bitmap itself (and the kernel) as used so they
 * are never handed out as allocatable frames.
 */
void    pmm_init(multiboot_info_t *mbi);

/*
 * pmm_alloc_frame - allocate one free physical 4 KB frame.
 *
 * Returns the physical address of the frame, or 0 on failure.
 * Triggers a kernel panic if out of memory.
 */
uint32_t pmm_alloc_frame(void);

/*
 * pmm_free_frame - release a previously allocated frame.
 *
 * @addr must be 4 KB-aligned and must have been returned by
 * pmm_alloc_frame().  Double-frees are detected and panic.
 */
void    pmm_free_frame(uint32_t addr);

/*
 * pmm_mark_region_used / pmm_mark_region_free
 *
 * Utility helpers to mark an arbitrary byte range as used/free.
 * Both functions round the range outward to full page boundaries.
 */
void    pmm_mark_region_used(uint32_t base, uint32_t length);
void    pmm_mark_region_free(uint32_t base, uint32_t length);


uint32_t pmm_free_frames(void);
uint32_t pmm_total_frames(void);

#endif /* PMM_H */
