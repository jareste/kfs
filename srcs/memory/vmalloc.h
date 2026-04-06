#ifndef VMALLOC_H
#define VMALLOC_H

#include "../utils/stdint.h"
/*
 * vmalloc manages a dedicated region of the KERNEL virtual address
 * space.  Physical frames are allocated from the PMM on demand and
 * mapped into this region; they do NOT need to be physically contiguous.
 *
 * Layout (kernel virtual space):
 *
 *   0xC0000000  KERNEL_VIRTUAL_BASE  (kernel code/data – identity-mapped)
 *   ...
 *   0xD0000000  VMALLOC_START        (vmalloc arena begins)
 *   0xF0000000  VMALLOC_END          (vmalloc arena ends – 512 MB)
 *
 * Adjust these if your kernel uses a different virtual map.
 */
#define VMALLOC_START   0xD0000000u
#define VMALLOC_END     0xF0000000u     /* 512 MB vmalloc arena */

/*
 * Each vmalloc() call produces one vregion_t that tracks the virtual
 * address range, its size, and whether it is free.
 *
 * The descriptors are kept in a singly-linked list sorted by virt_start.
 * We allocate the descriptors themselves via kmalloc().
 */
typedef struct vregion
{
    uint32_t        virt_start; /* first virtual address of this region */
    uint32_t        size;       /* byte length (always page-aligned)     */
    uint8_t         free;       /* 1 = available, 0 = in use            */
    struct vregion *next;
} vregion_t;

/*
 * vmalloc_init - initialise the virtual allocator.
 *
 * Must be called AFTER kmalloc_init() because region descriptors are
 * allocated from the kernel heap.
 */
void    vmalloc_init(void);

/*
 * vmalloc - allocate @size bytes of virtual address space.
 *
 * Rounds @size up to the nearest page boundary, finds a free virtual
 * range, backs each page with a physical frame from the PMM, and maps
 * all pages into the current kernel page directory.
 *
 * Returns the virtual start address of the allocation, or 0 on failure.
 * Panics on fatal errors (out of virtual space, out of physical memory).
 */
void   *vmalloc(uint32_t size);

/*
 * vfree - release a vmalloc() allocation.
 *
 * Unmaps every page in the region and returns all physical frames to
 * the PMM.  The virtual range is marked free and coalesced with
 * adjacent free ranges.
 */
void    vfree(void *ptr);

/*
 * vsize - return the byte size of a vmalloc() allocation.
 *
 * @ptr must be an address returned by vmalloc().
 */
uint32_t vsize(void *ptr);

/*
 * vbrk - adjust the "current position" in the vmalloc arena.
 *
 * This is the virtual-memory analogue of kbrk().  Unlike kbrk(), it
 * operates on the vmalloc arena rather than the kernel heap, and it
 * always works in whole-page increments.
 *
 * Positive @pages: extend the arena cursor by @pages pages, mapping
 *   new physical frames.  Returns the OLD cursor virtual address.
 * Negative @pages: unmap |pages| pages from the top of the arena and
 *   return their frames to the PMM.
 *
 * Returns the old cursor on success, 0 on failure.
 */
uint32_t vbrk(int32_t pages);

/* Diagnostics */
void     vmalloc_dump(void);
uint32_t vmalloc_free_bytes(void);
uint32_t vmalloc_used_bytes(void);

#endif /* VMALLOC_H */
