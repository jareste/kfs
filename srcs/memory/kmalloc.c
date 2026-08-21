#include "kmalloc.h"
#include "vmm.h"
#include "pmm.h"
#include "../utils/utils.h"
#include "../panic/kpanic.h"
#include "../display/display.h"
#include "../keyboard/idt.h"
#include "../tasks/task.h"

/* ------------------------------------------------------------------ */
/*  Locals                                                            */
/* ------------------------------------------------------------------ */

static uint32_t heap_start = 0; /* first byte of the heap        */
static uint32_t heap_end   = 0; /* current break (one past last) */
static uint32_t heap_max   = 0; /* absolute ceiling              */
static block_header_t* heap_head = 0; /* first block in the list       */
static uint32_t m_alloc_id_counter = 0;
static uint32_t m_total_allocs     = 0;
static uint32_t m_total_frees      = 0;
static uint32_t m_peak_used_bytes  = 0;

/* ------------------------------------------------------------------ */
/*  Utils                                                             */
/* ------------------------------------------------------------------ */
#define KMALLOC_POISON_BYTE 0xDEu

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

static void write_redzone(block_header_t *b)
{
    uint32_t *rz = (uint32_t *)((uint8_t *)ptr_from_block(b) + b->requested_size);
    rz[0] = KMALLOC_REDZONE_WORD;
    rz[1] = KMALLOC_REDZONE_WORD;
}

static int redzone_valid(block_header_t *b)
{
    uint32_t *rz = (uint32_t *)((uint8_t *)ptr_from_block(b) + b->requested_size);
    return rz[0] == KMALLOC_REDZONE_WORD && rz[1] == KMALLOC_REDZONE_WORD;
}

static uint32_t used_bytes_locked(void)
{
    uint32_t total = 0;
    block_header_t *b;

    for (b = heap_head; b && block_valid(b); b = b->next)
        if (!b->free)
            total += b->size;
    return total;
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
    uint32_t flags;

    if (increment == 0)
        return old_break;

    flags = irq_save();

    new_break_signed = (int32_t)heap_end + increment;

    /* Underflow / overflow guards */
    if (new_break_signed < (int32_t)heap_start)
    {
        kpanic("kbrk: cannot shrink below heap_start", 0);
        return 0;
    }
    if ((uint32_t)new_break_signed > heap_max)
    {
        kprintf("kbrk: requested break %x exceeds heap_max %x\n",
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
    irq_restore(flags);
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
    heap_head->requested_size = 0;
    heap_head->alloc_caller = 0;
    heap_head->free_caller = 0;
    heap_head->alloc_id = 0; /* 0 = never allocated via kmalloc() */
    heap_head->owner_pid = (pid_t)-1;

    kprintf("[KMALLOC] heap @ %x - %x (max %x)\n",
           heap_start, heap_end, heap_max);
}

/* ------------------------------------------------------------------ */
/*  kmalloc                                                            */
/* ------------------------------------------------------------------ */

static void finish_alloc(block_header_t *b, uint32_t aligned_requested, void *caller)
{
    uint32_t used;
    task_t *current = get_current_task();

    b->requested_size = aligned_requested;
    b->alloc_caller    = caller;
    b->free_caller     = 0;
    b->alloc_id        = ++m_alloc_id_counter;
    b->owner_pid       = current ? (pid_t)current->pid : (pid_t)-1;
    write_redzone(b);

    m_total_allocs++;
    used = used_bytes_locked();
    if (used > m_peak_used_bytes)
        m_peak_used_bytes = used;
}

void *kmalloc(uint32_t requested)
{
    block_header_t *b;
    block_header_t *split;
    block_header_t *last;
    uint32_t old_break;
    uint32_t flags;
    uint32_t aligned_requested;
    uint32_t total_size;
    void *caller = __builtin_return_address(0);

    if (requested == 0)
        return 0;

    aligned_requested = ALIGN8(requested);       /* what the caller gets    */
    total_size        = aligned_requested + KMALLOC_REDZONE_SIZE; /* what we reserve */

    flags = irq_save();

    /* First-fit search */
    b = heap_head;
    while (b)
    {
        if (!block_valid(b))
            kpanic("kmalloc: heap corruption detected (bad magic)", 1);

        if (b->free && b->size >= total_size)
            break;
        b = b->next;
    }

    /* No suitable block found: expand the heap */
    if (!b)
    {
        old_break = kbrk((int32_t)(HEADER_SIZE + total_size));
        if (old_break == 0)
            kpanic("kmalloc: kbrk failed", 1);

        b = (block_header_t *)old_break;
        b->magic = BLOCK_MAGIC;
        b->size = total_size;
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

        finish_alloc(b, aligned_requested, caller);
        irq_restore(flags);
        return ptr_from_block(b);
    }

    /* ---- Split the block if there is enough room for a new header ---- */
    if (b->size >= total_size + HEADER_SIZE + 8)
    {
        split = (block_header_t *)((uint8_t *)b + HEADER_SIZE + total_size);
        split->magic = BLOCK_MAGIC;
        split->size = b->size - total_size - HEADER_SIZE;
        split->free = 1;
        split->next = b->next;
        split->prev = b;
        split->requested_size = 0;
        split->alloc_caller = 0;
        split->free_caller = 0;
        split->alloc_id = 0;
        split->owner_pid = (pid_t)-1;

        if (b->next)
            b->next->prev = split;
        b->next = split;
        b->size = total_size;
    }

    b->free = 0;
    finish_alloc(b, aligned_requested, caller);
    irq_restore(flags);
    return ptr_from_block(b);
}

/* ------------------------------------------------------------------ */
/*  kfree                                                              */
/* ------------------------------------------------------------------ */

void kfree(void *ptr)
{
    block_header_t *b;
    uint32_t flags;
    void *caller = __builtin_return_address(0);

    if (!ptr)
        return;

    b = block_from_ptr(ptr);

    flags = irq_save();

    if (!block_valid(b))
    {
        irq_restore(flags);
        kprintf("[KMALLOC] kfree(%x) by %x: not a live allocation (bad magic %x)\n",
                (uint32_t)ptr, (uint32_t)caller, b ? b->magic : 0);
        kpanic("kfree: invalid pointer (bad magic) - possible corruption", 0);
        return;
    }
    if (b->free)
    {
        irq_restore(flags);
        kprintf("[KMALLOC] double-free: %x (alloc #%d, %d bytes, allocated by %x) "
                "was already freed by %x -- now attempted again by %x\n",
                (uint32_t)ptr, b->alloc_id, b->requested_size,
                (uint32_t)b->alloc_caller, (uint32_t)b->free_caller, (uint32_t)caller);
        kpanic("kfree: double-free detected", 0);
        return;
    }
    if (!redzone_valid(b))
    {
        kprintf("[KMALLOC] heap buffer overflow: alloc #%d @ %x (%d bytes, allocated by %x) "
                "corrupted its own redzone -- caught on free by %x\n",
                b->alloc_id, (uint32_t)ptr, b->requested_size, (uint32_t)b->alloc_caller, (uint32_t)caller);
        irq_restore(flags);
        kpanic("kfree: redzone overflow detected -- buffer written past its end", 0);
        return;
    }

    b->free = 1;
    b->free_caller = caller;
    m_total_frees++;

    /* Best-effort UAF tripwire -- see KMALLOC_POISON_BYTE. */
    memset(ptr, KMALLOC_POISON_BYTE, b->requested_size);

    /* Merge with adjacent free blocks */
    coalesce(b);
    irq_restore(flags);
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

    return b->requested_size;
}

/* ------------------------------------------------------------------ */
/*  Diagnostics                                                        */
/* ------------------------------------------------------------------ */

void kmalloc_dump(void)
{
    block_header_t *b;
    uint32_t idx;

    kprintf("=== KMALLOC HEAP DUMP ===\n");
    kprintf("heap: %x - %x\n", heap_start, heap_end);

    b = heap_head;
    idx = 0;

    while (b)
    {
        if (!block_valid(b))
        {
            kprintf("  [%d] CORRUPTED BLOCK @ %x (magic=%x)\n", idx, (uint32_t)b, b->magic);
            break;
        }
        if (b->free)
        {
            if (b->alloc_id == 0)
                kprintf("  [%d] @ %x  size=%d  FREE  (never allocated)\n",
                        idx, (uint32_t)b, b->size);
            else
                kprintf("  [%d] @ %x  size=%d  FREE  (alloc #%d by %x, freed by %x)\n",
                        idx, (uint32_t)b, b->size, b->alloc_id,
                        (uint32_t)b->alloc_caller, (uint32_t)b->free_caller);
        }
        else
        {
            kprintf("  [%d] @ %x  size=%d  USED  alloc #%d by %x  owner_pid=%d\n",
                    idx, (uint32_t)b, b->requested_size, b->alloc_id, (uint32_t)b->alloc_caller, b->owner_pid);
        }
        b = b->next;
        idx++;
    }
    kprintf("=== END KMALLOC HEAP DUMP ===\n");
}

void kmalloc_dump_leaks(void)
{
    block_header_t *b;
    uint32_t count = 0;
    uint32_t total = 0;

    kprintf("=== LIVE ALLOCATIONS ===\n");
    for (b = heap_head; b; b = b->next)
    {
        if (!block_valid(b))
        {
            kprintf("  CORRUPTED BLOCK @ %x -- stopping\n", (uint32_t)b);
            break;
        }
        if (b->free)
            continue;

        kprintf("  alloc #%d @ %x  %d bytes  by %x  owner_pid=%d\n",
                b->alloc_id, (uint32_t)ptr_from_block(b), b->requested_size, (uint32_t)b->alloc_caller, b->owner_pid);
        count++;
        total += b->requested_size;
    }
    kprintf("=== %d live allocation(s), %d bytes total ===\n", count, total);
}

static uint32_t audit_walk(void)
{
    block_header_t *b;
    uint32_t problems = 0;
    uint32_t live_walked = 0;
    uint32_t expected_live;

    for (b = heap_head; b; b = b->next)
    {
        if (!block_valid(b))
        {
            kprintf("  [CORRUPT] block @ %x has a bad magic (%x) -- cannot trust the list past this point\n",
                    (uint32_t)b, b ? b->magic : 0);
            problems++;
            break;
        }

        if (!b->free)
        {
            live_walked++;
            if (!redzone_valid(b))
            {
                kprintf("  [OVERFLOW] alloc #%d @ %x (%d bytes, by %x) has a corrupted redzone\n",
                        b->alloc_id, (uint32_t)ptr_from_block(b), b->requested_size, (uint32_t)b->alloc_caller);
                problems++;
            }
        }

        if (b->next && b->next->prev != b)
        {
            kprintf("  [LINK] block @ %x and %x disagree about being neighbours\n",
                    (uint32_t)b, (uint32_t)b->next);
            problems++;
        }
    }

    expected_live = m_total_allocs - m_total_frees;
    if (live_walked != expected_live)
    {
        kprintf("  [MISMATCH] %d live block(s) found by walking the heap, but total_allocs-total_frees says %d\n",
                live_walked, expected_live);
        problems++;
    }

    return problems;
}

uint32_t kmalloc_audit(void)
{
    uint32_t problems;

    kprintf("=== KMALLOC AUDIT ===\n");
    problems = audit_walk();
    kprintf("=== AUDIT DONE: %d problem(s) found ===\n", problems);
    return problems;
}

uint32_t kmalloc_audit_quiet(void)
{
    return audit_walk();
}

uint32_t kmalloc_check_dead_owners(void)
{
    block_header_t *b;
    uint32_t problems = 0;

    for (b = heap_head; b && block_valid(b); b = b->next)
    {
        if (b->free || b->owner_pid == (pid_t)-1)
            continue;

        if (get_task_by_pid(b->owner_pid) == NULL)
        {
            kprintf("  [DEAD OWNER] alloc #%d @ %x (%d bytes, by %x) belongs to pid %d, which no longer exists\n",
                    b->alloc_id, (uint32_t)ptr_from_block(b), b->requested_size,
                    (uint32_t)b->alloc_caller, b->owner_pid);
            problems++;
        }
    }

    return problems;
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
    return used_bytes_locked();
}

uint32_t kmalloc_live_count(void)
{
    block_header_t *b;
    uint32_t count = 0;

    for (b = heap_head; b && block_valid(b); b = b->next)
        if (!b->free)
            count++;

    return count;
}

uint32_t kmalloc_total_allocs(void)
{
    return m_total_allocs;
}

uint32_t kmalloc_total_frees(void)
{
    return m_total_frees;
}

uint32_t kmalloc_peak_bytes(void)
{
    return m_peak_used_bytes;
}
