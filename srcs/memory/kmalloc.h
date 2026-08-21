#ifndef KMALLOC_H
#define KMALLOC_H

#include "../utils/stdint.h"
#include "../keyboard/signals.h" /* pid_t */

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

typedef struct block_header
{
    uint32_t            magic;
    uint32_t            size;
    uint32_t            requested_size;
    uint8_t             free;
    struct block_header *next;
    struct block_header *prev;
    void               *alloc_caller;
    void               *free_caller;
    uint32_t            alloc_id;
    pid_t               owner_pid;
} block_header_t;

#define BLOCK_MAGIC     0xCAFEBABEu
#define HEADER_SIZE     sizeof(block_header_t)
#define KMALLOC_REDZONE_SIZE   8u
#define KMALLOC_REDZONE_WORD   0xFEEDFACEu

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
 * kstrdup - duplicate a NUL-terminated string onto the kernel heap.
 *
 * Returns a kmalloc()'d copy of @s, or NULL on failure.
 */
void   *kstrdup(const char *s);

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
 * Detects and panics on: a bad/foreign pointer, a double-free (and
 * reports who freed it the first time), and a redzone overflow (the
 * caller wrote past the end of its own buffer).
 */
void    kfree(void *ptr);

/*
 * ksize - return the usable size of an allocated block (the size the
 * caller originally asked for, rounded up to 8 bytes -- not including
 * the hidden redzone).
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

/* ------------------------------------------------------------------ */
/*  Diagnostics                                                        */
/* ------------------------------------------------------------------ */

void     kmalloc_dump(void);        /* print the full heap block list, incl. owner/id */
void     kmalloc_dump_leaks(void);  /* print every currently-LIVE allocation only */

/*
 * kmalloc_audit - walk the whole heap validating every block's magic
 * AND redzone (used or free), independent of whatever kmalloc()/kfree()
 * happen to touch next. Reports each problem it finds via kprintf and
 * keeps going (does not stop at the first one, does not panic) so a
 * single audit call gives a full picture. Returns the number of
 * problems found (0 = clean).
 *
 * Safe to call at any time, e.g. periodically or from a debug command,
 * to catch corruption near where it actually happened instead of
 * however many allocations later something else trips over it.
 */
uint32_t kmalloc_audit(void);

/*
 * kmalloc_audit_quiet - identical check to kmalloc_audit(), but prints
 * nothing at all when the heap is clean (still reports each problem it
 * does find, same as kmalloc_audit()). Meant for a periodic/background
 * caller, where a "still clean" banner every few seconds would just be
 * noise -- see mem_check.c.
 */
uint32_t kmalloc_audit_quiet(void);

/*
 * kmalloc_check_dead_owners - walk the live allocations and flag any
 * whose owner_pid no longer belongs to a live task (get_task_by_pid()
 * returns NULL for it) -- i.e. memory a task allocated for itself and
 * never freed before exiting. PIDs in this kernel are never reused, so
 * "owner task not found" is unambiguous. Allocations made with no task
 * context (owner_pid == -1, e.g. boot-time / not attributable to any
 * one task) are never flagged.
 *
 * Prints each one it finds (alloc id, size, owner pid, allocator) and
 * returns the count (0 = none found). Prints nothing when there's
 * nothing to report.
 */
uint32_t kmalloc_check_dead_owners(void);

uint32_t kmalloc_free_bytes(void);
uint32_t kmalloc_used_bytes(void);
uint32_t kmalloc_live_count(void);    /* number of currently-live allocations */
uint32_t kmalloc_total_allocs(void);  /* every kmalloc() ever made, monotonic */
uint32_t kmalloc_total_frees(void);   /* every successful kfree() ever made, monotonic */
uint32_t kmalloc_peak_bytes(void);    /* high-water mark of kmalloc_used_bytes() */

#endif /* KMALLOC_H */
