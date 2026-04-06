#include "pmm.h"
#include "../panic/kpanic.h"
#include "../utils/utils.h"
#include "../display/display.h"

extern uint32_t _kernel_end;

/*
 * Bitmap layout
 * -------------
 * Each uint32_t word holds 32 bits → 32 frames.
 * Bit 0 of word 0 = frame 0 (physical address 0x00000000).
 * Bit k of word i = frame (i*32 + k).
 * 0 = free, 1 = used.
 *
 * The bitmap itself lives in physical memory right after the kernel,
 * at the address stored in pmm_bitmap.  Its size in bytes is:
 *   (total_frames / 8)  rounded up to a multiple of PAGE_SIZE.
 */

static uint32_t *pmm_bitmap  = 0;   /* physical address of bitmap data   */
static uint32_t  total_frames = 0;  /* total number of 4 KB frames       */
static uint32_t  free_count   = 0;  /* frames currently free             */

/* ------------------------------------------------------------------ */
/*  helpers                                                           */
/* ------------------------------------------------------------------ */

#define FRAMES_PER_WORD     32u
#define BITMAP_WORD(frame)  ((frame) / FRAMES_PER_WORD)
#define BITMAP_BIT(frame)   ((frame) % FRAMES_PER_WORD)

static inline void bitmap_set(uint32_t frame)
{
    pmm_bitmap[BITMAP_WORD(frame)] |= (1u << BITMAP_BIT(frame));
}

static inline void bitmap_clear(uint32_t frame)
{
    pmm_bitmap[BITMAP_WORD(frame)] &= ~(1u << BITMAP_BIT(frame));
}

static inline int bitmap_test(uint32_t frame)
{
    return (pmm_bitmap[BITMAP_WORD(frame)] >> BITMAP_BIT(frame)) & 1u;
}

/*
 * Find the first free (0) bit in the bitmap.
 * Returns the frame index, or (uint32_t)-1 if none found.
 */
static uint32_t bitmap_first_free(void)
{
    uint32_t i;
    uint32_t bit;
    uint32_t frame;
    uint32_t words = (total_frames + FRAMES_PER_WORD - 1) / FRAMES_PER_WORD;

    for (i = 0; i < words; i++)
    {
        if (pmm_bitmap[i] == 0xFFFFFFFF)
            continue; /* all used in this word */

        for (bit = 0; bit < FRAMES_PER_WORD; bit++)
        {
            if (!((pmm_bitmap[i] >> bit) & 1u))
            {
                frame = i * FRAMES_PER_WORD + bit;
                if (frame < total_frames)
                    return frame;
            }
        }
    }
    return (uint32_t)-1;
}

/* ------------------------------------------------------------------ */
/*  Public helpers                                                     */
/* ------------------------------------------------------------------ */

/*
 * Mark every page frame covered by [base, base+length) as USED.
 * Rounds outward to page boundaries.
 */
void pmm_mark_region_used(uint32_t base, uint32_t length)
{
    uint32_t f;
    uint32_t frame_start = base / PAGE_SIZE;
    uint32_t frame_end   = (base + length + PAGE_SIZE - 1) / PAGE_SIZE;

    for (f = frame_start; f < frame_end && f < total_frames; f++)
    {
        if (!bitmap_test(f))
        {
            bitmap_set(f);
            if (free_count > 0)
                free_count--;
        }
    }
}

/*
 * Mark every page frame covered by [base, base+length) as FREE.
 * Rounds outward to page boundaries.
 */
void pmm_mark_region_free(uint32_t base, uint32_t length)
{
    uint32_t f;
    uint32_t frame_start = base / PAGE_SIZE;
    uint32_t frame_end   = (base + length + PAGE_SIZE - 1) / PAGE_SIZE;

    for (f = frame_start; f < frame_end && f < total_frames; f++)
    {
        if (bitmap_test(f))
        {
            bitmap_clear(f);
            free_count++;
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Initialisation                                                     */
/* ------------------------------------------------------------------ */

void pmm_init(multiboot_info_t *mbi)
{
    multiboot_mmap_entry_t* entry;
    uint32_t total_memory;
    uint32_t bitmap_bytes;
    uint32_t region_base;
    uint32_t region_len;

    if (!(mbi->flags & MULTIBOOT_FLAG_MEM))
        kpanic("PMM: multiboot memory info not available", 1);

    total_memory = (1024 + mbi->mem_upper) * 1024; /* bytes */
    total_frames = total_memory / PAGE_SIZE;

    /* Align and place the bitmap right after the kernel in memory. */
    pmm_bitmap = (uint32_t *)PAGE_ALIGN((uint32_t)&_kernel_end);

    bitmap_bytes = (total_frames + 7) / 8; /* bits→bytes */
    bitmap_bytes = PAGE_ALIGN(bitmap_bytes); /* page-align */

    /* mark everything used by default, then we'll free the AVAILABLE regions below */
    memset(pmm_bitmap, 0xFF, bitmap_bytes);
    free_count = 0;

    /* Walk the GRUB mem map and mark available regions as free.
     * Each entry is accessed via pointer arithmetic because the entry's own 'size' field tells us how far to step. 
     */
    if (mbi->flags & MULTIBOOT_FLAG_MMAP)
    {
        entry = (multiboot_mmap_entry_t *)(uint32_t)mbi->mmap_addr;

        while ((uint32_t)entry < mbi->mmap_addr + mbi->mmap_length)
        {
            /* Handle only the lower 32 bits of addr/len.
             * we are 32-bit kernel so ignore anything above 4 GB.
             */
            if ((entry->addr_high == 0) &&
                (entry->type == MULTIBOOT_MEMORY_AVAILABLE))
            {
                region_base = entry->addr_low;
                region_len  = entry->len_low;

                /*
                 * Skip the null page (frame 0) deliberately so we never
                 * hand out physical address 0 - it is used as an error
                 * sentinel by pmm_alloc_frame().
                 */
                if (region_base == 0 && region_len > PAGE_SIZE)
                {
                    region_base += PAGE_SIZE;
                    region_len  -= PAGE_SIZE;
                }

                pmm_mark_region_free(region_base, region_len);
            }

            /* Advance to next entry: entry->size does NOT include itself */
            entry = (multiboot_mmap_entry_t *)((uint32_t)entry + entry->size + sizeof(entry->size));
        }
    }
    else
    {
        /* Should never happen and have'nt been properly tested as so.
         * GRUB should always provide a memory map, but if it doesn't, we can still work with the basic mem_lower/mem_upper info.
         * Fallback: no mmap.  Trust mem_lower/mem_upper.
         * Free conventional memory (0x500 – 640 KB)
         * and extended memory (1 MB – top of RAM).
         */
        pmm_mark_region_free(0x500,    mbi->mem_lower * 1024 - 0x500);
        pmm_mark_region_free(0x100000, mbi->mem_upper * 1024);
    }

    /* Mark the bitmap region itself as used, so it won't be allocated for other purposes. */
    pmm_mark_region_used(0x100000, (uint32_t)pmm_bitmap - 0x100000);
    pmm_mark_region_used((uint32_t)pmm_bitmap, bitmap_bytes);

    /* Also protect low memory (first 1 MB) – BIOS/GRUB data lives there */
    pmm_mark_region_used(0x0, 0x100000);

    printf("[PMM] total frames : %d\n", total_frames);
    printf("[PMM] free  frames : %d\n", free_count);
    printf("[PMM] bitmap @ 0x%x (%d bytes)\n", (uint32_t)pmm_bitmap, bitmap_bytes);
}

/* ------------------------------------------------------------------ */
/*  Allocation / deallocation                                          */
/* ------------------------------------------------------------------ */

uint32_t pmm_alloc_frame(void)
{
    uint32_t frame;

    if (free_count == 0)
        kpanic("PMM: out of physical memory", 1);

    frame = bitmap_first_free();
    if (frame == (uint32_t)-1)
        kpanic("PMM: bitmap inconsistency (free_count > 0 but no free bit)", 1);

    bitmap_set(frame);
    free_count--;

    return frame * PAGE_SIZE; /* return physical address */
}

void pmm_free_frame(uint32_t addr)
{
    uint32_t frame;

    if (addr % PAGE_SIZE != 0)
        kpanic("PMM: pmm_free_frame called with non-aligned address", 0);

    frame = addr / PAGE_SIZE;

    if (frame >= total_frames)
        kpanic("PMM: pmm_free_frame called with out-of-range address", 0);

    if (!bitmap_test(frame))
        kpanic("PMM: double-free detected in pmm_free_frame", 0);

    bitmap_clear(frame);
    free_count++;
}

/* ------------------------------------------------------------------ */
/*  Info/CLI commands                                                 */
/* ------------------------------------------------------------------ */

uint32_t pmm_free_frames(void)  { return free_count;   }
uint32_t pmm_total_frames(void) { return total_frames; }
