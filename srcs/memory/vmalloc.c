#include "vmalloc.h"
#include "vmm.h"
#include "pmm.h"
#include "kmalloc.h"
#include "../utils/utils.h"
#include "../panic/kpanic.h"
#include "../display/display.h"

/* ------------------------------------------------------------------ */
/*  Internal state                                                     */
/* ------------------------------------------------------------------ */

/*
 * The region list is sorted by virt_start in ascending order.
 * There is always at least one node (the entire free arena) after init.
 *
 * vbrk_cursor tracks the independent "vbrk arena" cursor which is
 * separate from the vmalloc free-list.  vbrk() maps/unmaps pages
 * above this cursor without involving the free-list at all.
 */
static vregion_t *region_list  = 0;
static uint32_t   vbrk_cursor  = VMALLOC_START;  /* start of vbrk arena */

/* ------------------------------------------------------------------ */
/*  helpers                                                           */
/* ------------------------------------------------------------------ */
static vregion_t *new_region(uint32_t start, uint32_t size, uint8_t free)
{
    vregion_t *r = (vregion_t *)kmalloc(sizeof(vregion_t));
    if (!r)
        kpanic("vmalloc: kmalloc failed allocating vregion descriptor", 1);
    r->virt_start = start;
    r->size = size;
    r->free = free;
    r->next = NULL;
    return r;
}

/*
 * Find the region whose virt_start matches @addr exactly.
 */
static vregion_t *find_region(uint32_t addr)
{
    vregion_t *r = region_list;
    while (r)
    {
        if (r->virt_start == addr)
            return r;
        r = r->next;
    }
    return NULL;
}

/*
 * Insert @r into the region list, maintaining virt_start order.
 */
static void insert_region(vregion_t *r)
{
    vregion_t* cur;

    /* Insert at head */
    if (!region_list || r->virt_start < region_list->virt_start)
    {
        r->next = region_list;
        region_list = r;
        return;
    }

    cur = region_list;
    while (cur->next && cur->next->virt_start < r->virt_start)
        cur = cur->next;

    r->next   = cur->next;
    cur->next = r;
}

/*
 * Coalesce adjacent free regions to reduce fragmentation.
 * Must be called after marking a region free.
 */
static void coalesce_regions(void)
{
    vregion_t* dead;
    vregion_t* r = region_list;

    while (r && r->next)
    {
        if (r->free && r->next->free &&
            r->virt_start + r->size == r->next->virt_start)
        {
            /* Absorb r->next into r */
            dead = r->next;
            r->size += dead->size;
            r->next  = dead->next;
            kfree(dead);   /* return descriptor to kernel heap */
            /* Don't advance r: check again in case three in a row */
        }
        else
        {
            r = r->next;
        }
    }
}

static void map_region_pages(uint32_t virt_start, uint32_t pages)
{
    uint32_t va;
    uint32_t i;
    uint32_t phys;

    for (i = 0; i < pages; i++)
    {
        va = virt_start + i * PAGE_SIZE;
        phys = pmm_alloc_frame();
        if (phys == 0)
            kpanic("vmalloc: out of physical memory", 1);
        vmm_map_page(vmm_current_directory(), va, phys, PAGE_KERNEL_RW);
    }
}

static void unmap_region_pages(uint32_t virt_start, uint32_t pages)
{
    uint32_t i;
    uint32_t va;

    for (i = 0; i < pages; i++)
    {
        va = virt_start + i * PAGE_SIZE;
        vmm_free_page(vmm_current_directory(), va);
    }
}

void vmalloc_init(void)
{
    kprintf("Initializing vmalloc at %u\n", vbrk_cursor);
    region_list  = new_region(VMALLOC_START,
                               VMALLOC_END - VMALLOC_START,
                               1 /* free */);
    vbrk_cursor  = VMALLOC_START;

    kprintf("[VMALLOC] arena %x - %x (%d MB)\n",
           VMALLOC_START, VMALLOC_END,
           (VMALLOC_END - VMALLOC_START) / (1024 * 1024));
}


void *vmalloc(uint32_t size)
{
    vregion_t *r;
    vregion_t *remainder;

    if (size == 0)
        return 0;

    /* Round up to whole pages */
    size = PAGE_ALIGN(size);

    /* First-fit search for a free region large enough */
    r = region_list;
    while (r)
    {
        if (r->free && r->size >= size)
            break;
        r = r->next;
    }

    if (!r)
    {
        kpanic("vmalloc: virtual address space exhausted", 1);
        return 0;
    }

    /* Split the region if there is leftover space */
    if (r->size > size)
    {
        remainder = new_region(r->virt_start + size,
                                          r->size - size,
                                          1 /* free */);
        /* Insert remainder right after r */
        remainder->next = r->next;
        r->next = remainder;
        r->size = size;
    }

    r->free = 0;

    /* Back every page in this region with a physical frame */
    map_region_pages(r->virt_start, size / PAGE_SIZE);

    return (void *)r->virt_start;
}

void vfree(void *ptr)
{
    uint32_t   addr;
    vregion_t *r;

    if (!ptr)
        return;

    addr = (uint32_t)ptr;
    r = find_region(addr);

    if (!r)
    {
        kpanic("vfree: pointer not found in vmalloc region list", 0);
        return;
    }
    if (r->free)
    {
        kpanic("vfree: double-free detected", 0);
        return;
    }

    /* Unmap and release all physical frames */
    unmap_region_pages(r->virt_start, r->size / PAGE_SIZE);

    r->free = 1;

    /* Merge adjacent free regions */
    coalesce_regions();
}

uint32_t vsize(void *ptr)
{
    vregion_t *r;

    if (!ptr)
        return 0;

    r = find_region((uint32_t)ptr);
    if (!r)
    {
        kpanic("vsize: pointer not found in vmalloc region list", 0);
        return 0;
    }

    return r->size;
}

uint32_t vbrk(int32_t pages)
{
    uint32_t bytes_needed;
    uint32_t i;
    uint32_t va;
    uint32_t phys;
    uint32_t to_free;
    uint32_t bytes;
    uint32_t old_cursor = vbrk_cursor;

    if (pages == 0)
        return old_cursor;

    if (pages > 0)
    {
        bytes_needed = (uint32_t)pages * PAGE_SIZE;

        if (vbrk_cursor + bytes_needed > VMALLOC_END)
        {
            kpanic("vbrk: would exceed VMALLOC_END", 0);
            return 0;
        }

        /* Map pages one by one from the PMM */
        for (i = 0; i < pages; i++)
        {
            va = vbrk_cursor + (uint32_t)i * PAGE_SIZE;
            phys = pmm_alloc_frame();
            if (phys == 0)
                kpanic("vbrk: out of physical memory", 1);
            vmm_map_page(vmm_current_directory(), va, phys, PAGE_KERNEL_RW);
        }

        vbrk_cursor += bytes_needed;

    }
    else
    {
        /* pages is negative: free |pages| pages from the top */
        to_free = (uint32_t)(-pages);
        bytes = to_free * PAGE_SIZE;

        if (bytes > vbrk_cursor - VMALLOC_START)
        {
            kpanic("vbrk: cannot shrink below VMALLOC_START", 0);
            return 0;
        }

        vbrk_cursor -= bytes;

        for (i = 0; i < to_free; i++)
        {
            va = vbrk_cursor + i * PAGE_SIZE;
            if (vmm_get_physical(vmm_current_directory(), va) != 0)
                vmm_free_page(vmm_current_directory(), va);
        }
    }

    return old_cursor;
}

/* ------------------------------------------------------------------ */
/*  Helpers/CLI commands                                              */
/* ------------------------------------------------------------------ */

void vmalloc_dump(void)
{
    vregion_t* r;
    uint32_t i;
    kprintf("=== VMALLOC REGION DUMP ===\n");
    kprintf("arena: 0x%x - 0x%x\n", VMALLOC_START, VMALLOC_END);
    kprintf("vbrk cursor: 0x%x\n", vbrk_cursor);

    r = region_list;
    i = 0;
    while (r)
    {
        kprintf("  [%d] %x - %x  size=%d  %s\n",
               i,
               r->virt_start,
               r->virt_start + r->size,
               r->size,
               r->free ? "FREE" : "USED");
        r = r->next;
        i++;
    }
    kprintf("=== END VMALLOC DUMP ===\n");
}

uint32_t vmalloc_free_bytes(void)
{
    uint32_t total = 0;
    vregion_t* r = region_list;
    while (r)
    {
        if (r->free)
            total += r->size;
        r = r->next;
    }
    return total;
}

uint32_t vmalloc_used_bytes(void)
{
    uint32_t total = 0;
    vregion_t *r = region_list;
    while (r)
    {
        if (!r->free)
            total += r->size;
        r = r->next;
    }
    return total;
}
