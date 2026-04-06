#ifndef KMALLOC_H
#define KMALLOC_H

#include "../utils/stdint.h"

/* ------------------------------------------------------------------ */
/*  Kernel heap layout                                                 */
/* ------------------------------------------------------------------ */

/*
 * The kernel heap starts right after the PMM bitmap in physical memory.
 * kbrk() manages the heap's top boundary (the "break").
 *
 * Heap layout in physical / virtual memory (identity-mapped):
 *
 *   [ kernel image ] [ PMM bitmap ] [ heap → → → → ]
 *                                   ^               ^
 *                                HEAP_START     heap_break
 *
 * HEAP_START is determined at runtime by kmalloc_init().
 * HEAP_MAX caps the total kernel heap size.
 */
#define KERNEL_HEAP_MAX     (32 * 1024 * 1024)  /* 32 MB hard cap */

/* ------------------------------------------------------------------ */
/*  Block header                                                       */
/* ------------------------------------------------------------------ */

/*
 * Every allocation is preceded by a block_header_t in memory.
 *
 *   [ block_header_t | <user data ...> ]
 *
 * The linked list chains all blocks (free and used) in address order,
 * which makes coalescing adjacent free blocks straightforward.
 */
typedef struct block_header
{
    uint32_t            magic;   /* sanity sentinel: BLOCK_MAGIC        */
    uint32_t            size;    /* usable bytes AFTER the header       */
    uint8_t             free;    /* 1 = free, 0 = in use                */
    struct block_header *next;   /* next block in the heap (or NULL)    */
    struct block_header *prev;   /* previous block in the heap (or NULL)*/
} block_header_t;

#define BLOCK_MAGIC     0xCAFEBABEu
#define HEADER_SIZE     sizeof(block_header_t)

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

/*
 * kmalloc_init - initialise the kernel heap.
 *
 * Must be called AFTER pmm_init() and vmm_init().
 * @heap_start  – first virtual address available for the heap.
 *                Pass 0 to let the allocator choose (recommended).
 */
void    kmalloc_init(uint32_t heap_start);

/*
 * kmalloc - allocate @size bytes from the kernel heap.
 *
 * Returns a pointer to the usable region (just past the header).
 * Panics if the heap is exhausted.  Never returns NULL.
 */
void   *kmalloc(uint32_t size);

/*
 * kfree - release a block previously returned by kmalloc().
 *
 * Double-free and bad-pointer are detected via the magic sentinel
 * and will trigger a non-fatal kernel panic.
 */
void    kfree(void *ptr);

/*
 * ksize - return the usable size of an allocated block.
 *
 * @ptr must be a pointer returned by kmalloc().
 */
uint32_t ksize(void *ptr);

/*
 * kbrk - move the heap break by @increment bytes.
 *
 * Positive increment: expand the heap (maps new pages if needed).
 * Negative increment: shrink the heap (unmaps pages if possible).
 * Returns the OLD break address (like sbrk on Unix), or 0 on failure.
 *
 * This is the low-level primitive; prefer kmalloc/kfree in normal code.
 */
uint32_t kbrk(int32_t increment);

/* Diagnostics */
void     kmalloc_dump(void);   /* print the full heap block list       */
uint32_t kmalloc_free_bytes(void);
uint32_t kmalloc_used_bytes(void);

#endif /* KMALLOC_H */
