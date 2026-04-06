#include "kmalloc.h"
#include "vmm.h"
#include "pmm.h"
#include "../utils/utils.h"
#include "../panic/kpanic.h"
#include "../display/display.h"

/* ------------------------------------------------------------------ */
/*  Locals                                                            */
/* ------------------------------------------------------------------ */

static uint32_t heap_start = 0; /* first byte of the heap        */
static uint32_t heap_end   = 0; /* current break (one past last) */
static uint32_t heap_max   = 0; /* absolute ceiling              */
static block_header_t* heap_head = 0; /* first block in the list       */

/* ------------------------------------------------------------------ */
/*  Utils                                                             */
/* ------------------------------------------------------------------ */

#define ALIGN8(n)   (((n) + 7u) & ~7u)

static block_header_t *block_from_ptr(void *ptr)
{
    return (block_header_t *)((uint8_t *)ptr - HEADER_SIZE);
}

static void *ptr_from_block(block_header_t *b)
{
    return (void *)((uint8_t *)b + HEADER_SIZE);
}

static int block_valid(block_header_t *b)
{
    return b && b->magic == BLOCK_MAGIC;
}

/* ------------------------------------------------------------------ */
/*  kbrk                                                              */
/* ------------------------------------------------------------------ */

uint32_t kbrk(int32_t increment)
{
    int32_t new_break_signed;
    uint32_t new_break;
    uint32_t map_from;
    uint32_t unmap_from;
    uint32_t map_to;
    uint32_t unmap_to;
    uint32_t va;
    uint32_t old_break = heap_end;

    if (increment == 0)
        return old_break;

    new_break_signed = (int32_t)heap_end + increment;

    /* Underflow / overflow guards */
    if (new_break_signed < (int32_t)heap_start)
    {
        kpanic("kbrk: cannot shrink below heap_start", 0);
        return 0;
    }
    if ((uint32_t)new_break_signed > heap_max)
    {
        printf("kbrk: requested break %x exceeds heap_max %x\n",
               (uint32_t)new_break_signed, heap_max);
        kpanic("kbrk: kernel heap exhausted (hit KERNEL_HEAP_MAX)", 1);
        return 0;
    }

    new_break = (uint32_t)new_break_signed;

    if (increment > 0)
    {
        /*
         * Map any new pages needed between old heap_end and new_break.
         * heap_end and new_break may not be page-aligned, so we walk
         * one page at a time from the first unmapped page.
         */
        map_from = PAGE_ALIGN(old_break);
        map_to   = PAGE_ALIGN(new_break);

        for (va = map_from; va < map_to; va += PAGE_SIZE)
        {
            /* Only map if not already mapped */
            if (vmm_get_physical(vmm_current_directory(), va) == 0)
                vmm_alloc_page(vmm_current_directory(), va, PAGE_KERNEL_RW);
        }
    }
    else
    {
        /*
         * Unmap pages that are now entirely above new_break.
         * A page is unmapped only when new_break is below its start.
         */
        unmap_from = PAGE_ALIGN(new_break);
        unmap_to   = PAGE_ALIGN(old_break);

        for (va = unmap_from; va < unmap_to; va += PAGE_SIZE)
        {
            if (vmm_get_physical(vmm_current_directory(), va) != 0)
                vmm_free_page(vmm_current_directory(), va);
        }
    }

    heap_end = new_break;
    return old_break;
}

/* ------------------------------------------------------------------ */
/*  Coalescing                                                        */
/* ------------------------------------------------------------------ */

/*
 * Try to merge block @b with its successor if both are free.
 */
static void coalesce_next(block_header_t *b)
{
    block_header_t *next;

    if (!b || !b->next)
        return;
    if (!b->free || !b->next->free)
        return;

    next = b->next;

    /* Absorb next into b */
    b->size += HEADER_SIZE + next->size;
    b->next  = next->next;
    if (next->next)
        next->next->prev = b;

    /* mark it so in case of usage after free would be easy to catchup */
    next->magic = 0xDEADDEAD;
}

/*
 * Coalesce @b with both neighbours if possible.
 */
static void coalesce(block_header_t *b)
{
    if (b->prev && b->prev->free)
        coalesce_next(b->prev);
    else
        coalesce_next(b);
}

/* ------------------------------------------------------------------ */
/*  Initialisation                                                     */
/* ------------------------------------------------------------------ */

void kmalloc_init(uint32_t start)
{
    if (start == 0)
    {
        /*
         * Default: place the heap right after the first page that the
         * VMM's identity mapping covers.  In practice the PMM bitmap
         * ends somewhere after _kernel_end; we pick a round virtual
         * address well above any kernel data.
         *
         * We use 32 MB as a safe default start so the heap never
         * collides with the PMM bitmap regardless of kernel size.
         */
        start = 32 * 1024 * 1024; /* 32 MB */
    }

    heap_start = PAGE_ALIGN(start);
    heap_end = heap_start;
    heap_max = heap_start + KERNEL_HEAP_MAX;
    heap_head = 0;

    /*
     * Bootstrap: create one large free block that spans the first page.
     * kbrk() will expand this as needed.
     */
    kbrk(PAGE_SIZE);   /* reserve first page */

    heap_head = (block_header_t *)heap_start;
    heap_head->magic = BLOCK_MAGIC;
    heap_head->size = PAGE_SIZE - HEADER_SIZE;
    heap_head->free = 1;
    heap_head->next = 0;
    heap_head->prev = 0;

    printf("[KMALLOC] heap @ %x - %x (max %x)\n",
           heap_start, heap_end, heap_max);
}

/* ------------------------------------------------------------------ */
/*  kmalloc                                                            */
/* ------------------------------------------------------------------ */

void *kmalloc(uint32_t size)
{
    block_header_t *b;
    block_header_t *split;
    block_header_t *last;
    uint32_t old_break;

    if (size == 0)
        return 0;

    size = ALIGN8(size);  /* keep allocations 8-byte aligned */

    /* First-fit search */
    b = heap_head;
    while (b)
    {
        if (!block_valid(b))
            kpanic("kmalloc: heap corruption detected (bad magic)", 1);

        if (b->free && b->size >= size)
            break;
        b = b->next;
    }

    /* No suitable block found: expand the heap */
    if (!b)
    {
        old_break = kbrk((int32_t)(HEADER_SIZE + size));
        if (old_break == 0)
            kpanic("kmalloc: kbrk failed", 1);

        b = (block_header_t *)old_break;
        b->magic = BLOCK_MAGIC;
        b->size = size;
        b->free = 0;
        b->next = 0;
        b->prev = 0;

        /* Link into the list */
        if (heap_head == 0)
        {
            heap_head = b;
        }
        else
        {
            /* Find the last block */
            last = heap_head;
            while (last->next)
                last = last->next;
            last->next = b;
            b->prev = last;
        }

        return ptr_from_block(b);
    }

    /* ---- Split the block if there is enough room for a new header ---- */
    if (b->size >= size + HEADER_SIZE + 8)
    {
        split = (block_header_t *)((uint8_t *)b + HEADER_SIZE + size);
        split->magic = BLOCK_MAGIC;
        split->size = b->size - size - HEADER_SIZE;
        split->free = 1;
        split->next = b->next;
        split->prev = b;

        if (b->next)
            b->next->prev = split;
        b->next = split;
        b->size = size;
    }

    b->free = 0;
    return ptr_from_block(b);
}

/* ------------------------------------------------------------------ */
/*  kfree                                                              */
/* ------------------------------------------------------------------ */

void kfree(void *ptr)
{
    block_header_t *b;

    if (!ptr)
        return;

    b = block_from_ptr(ptr);

    if (!block_valid(b))
    {
        kpanic("kfree: invalid pointer (bad magic) - possible corruption", 0);
        return;
    }
    if (b->free)
    {
        kpanic("kfree: double-free detected", 0);
        return;
    }

    b->free = 1;

    /* Merge with adjacent free blocks */
    coalesce(b);
}

/* ------------------------------------------------------------------ */
/*  ksize                                                              */
/* ------------------------------------------------------------------ */

uint32_t ksize(void *ptr)
{
    block_header_t *b;

    if (!ptr)
        return 0;

    b = block_from_ptr(ptr);

    if (!block_valid(b))
    {
        kpanic("ksize: invalid pointer", 0);
        return 0;
    }

    return b->size;
}

/* ------------------------------------------------------------------ */
/*  Helpers/CLI commands                                              */
/* ------------------------------------------------------------------ */

void kmalloc_dump(void)
{
    block_header_t *b;
    uint32_t idx;

    printf("=== KMALLOC HEAP DUMP ===\n");
    printf("heap: 0x%x - 0x%x\n", heap_start, heap_end);

    b = heap_head;
    idx = 0;

    while (b)
    {
        if (!block_valid(b))
        {
            printf("  [%d] CORRUPTED BLOCK @ 0x%x\n", idx, (uint32_t)b);
            break;
        }
        printf("  [%d] @ 0x%x  size=%-6d  %s\n",
               idx,
               (uint32_t)b,
               b->size,
               b->free ? "FREE" : "USED");
        b = b->next;
        idx++;
    }
    printf("=== END HEAP DUMP ===\n");
}

uint32_t kmalloc_free_bytes(void)
{
    uint32_t total = 0;
    block_header_t *b = heap_head;
    while (b)
    {
        if (b->free)
            total += b->size;
        b = b->next;
    }
    return total;
}

uint32_t kmalloc_used_bytes(void)
{
    uint32_t total = 0;
    block_header_t *b = heap_head;
    while (b)
    {
        if (!b->free)
            total += b->size;
        b = b->next;
    }
    return total;
}
