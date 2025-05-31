// e820.h
#include "../utils/stdint.h"
#include "../utils/utils.h"
#include "pmm.h"

#include "../display/display.h"

// typedef struct {
//     uint64_t addr;
//     uint64_t len;
//     uint32_t type;    // 1 = RAM, else reserved
//     uint32_t _pad;
// } __attribute__((packed)) e820_entry_t;

typedef struct {
    uint32_t size;   // ==20 on most BIOSes
    uint64_t addr;   // base of the region
    uint64_t len;    // length of the region
    uint32_t type;   // 1 = available RAM, others = reserved
} __attribute__((packed)) e820_entry_t;


// Globals set by boot stub:
uint32_t e820_count;
e820_entry_t *e820_map;

// Frame‐allocator state:
static uint32_t nframes;
static uint32_t *frame_bitmap;  // bitset size = (nframes+31)/32 words

// Bit ops:
static inline void set_frame(uint32_t f)   { frame_bitmap[f/32] |=  (1u<<(f&31)); }
static inline void clear_frame(uint32_t f) { frame_bitmap[f/32] &= ~(1u<<(f&31)); }
static inline int  test_frame(uint32_t f)  { return frame_bitmap[f/32] &   (1u<<(f&31)); }

// Find first zero bit
static int find_first_free(void) {
    uint32_t words = (nframes+31)/32;
    printf("nframes: %d, words: %d\n", nframes, words);
    for(uint32_t i=0;i<words;i++) {
        if (frame_bitmap[i] != 0xFFFFFFFFu) {
            for(int b=0;b<32;b++) {
                if (!(frame_bitmap[i] & (1u<<b)))
                    return i*32 + b;
            }
        }
    }
    return -1;
}

void *pmm_alloc_frame(void) {
    int f = find_first_free();
    if (f < 0) return NULL;
    set_frame(f);
    return (void*)(uintptr_t)(f * FRAME_SIZE);
}

void pmm_free_frame(void *addr) {
    uint32_t f = (uint32_t)((uintptr_t)addr / FRAME_SIZE);
    clear_frame(f);
}

// Called by boot stub:
void pmm_init(uint32_t mmap_addr, uint32_t mmap_length) {
    uint8_t *p      = (uint8_t*)(uintptr_t)mmap_addr;
    uint8_t *p_end  = p + mmap_length;
    uint64_t maxaddr = 0;
    int      entries = 0;

    // 1) Scan the raw E820 blob
    while (p < p_end) {
        e820_entry_t *e = (e820_entry_t*)p;
        entries++;
        if (e->type == 1) {
            uint64_t top = e->addr + e->len;
            if (top > maxaddr) 
                maxaddr = top;
        }
        // advance by entry->size (20) plus the 4-byte size field
        p += e->size + sizeof(e->size);
    }

    printf("PMM: saw %d entries, maxaddr = %x\n", entries, maxaddr);

    // … now you can compute nframes, carve out your bitmap, etc.
    nframes = (maxaddr + FRAME_SIZE - 1) / FRAME_SIZE;
    nframes = (maxaddr + FRAME_SIZE - 1) / FRAME_SIZE;

    // 2) Reserve space for bitmap just after kernel end.
    extern uint8_t endkernel;      // from linker script
    uint8_t _end = endkernel;
    uintptr_t bm_phys = ((uintptr_t)&_end + FRAME_SIZE -1) & ~(FRAME_SIZE-1);
    uint32_t map_words = (nframes + 31)/32;
    frame_bitmap = (uint32_t*)(bm_phys);

    // mark all frames “used” initially
    for(uint32_t i=0;i<map_words;i++) frame_bitmap[i]=0xFFFFFFFFu;

    // 3) Walk e820 again, clear bits in RAM regions
    for(uint32_t i=0;i<e820_count;i++){
        if (e820_map[i].type != 1) continue;
        uint64_t start = (e820_map[i].addr + FRAME_SIZE-1) / FRAME_SIZE;
        uint64_t end   = (e820_map[i].addr + e820_map[i].len) / FRAME_SIZE;
        for(uint64_t f=start; f<end; f++) clear_frame(f);
    }

    // 4) Also reserve frames for kernel & bitmap itself
    uint32_t first_free = (bm_phys/FRAME_SIZE);
    uint32_t last_used  = first_free + map_words;
    for(uint32_t f = first_free; f < last_used; f++)
        set_frame(f);

}
