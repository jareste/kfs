// kmem.c
#include "kmem.h"
#include "pmm.h"     // for FRAME_SIZE, pmm_alloc_frame()
#include "../kshell/kshell.h" // for kshell()
#include "../display/display.h" // for printf()

// void install_all_cmds(command_t* cmds, section_t section)

void m_test_kmem();

static command_t global_commands[] = {
    {"tkmem", "Display this help message", m_test_kmem},
    {NULL, NULL, NULL}    
};


typedef struct kmem_block {
    uint32_t size;             // total size of this block (including header)
    struct kmem_block *next;   // next free block
} kmem_block_t;

// Heap arena bounds (physical == virtual identity-mapped for now)
extern uint8_t endkernel;      // from linker script
static uintptr_t heap_start;   // = aligned endkernel
static uintptr_t heap_end;     // current top of heap
static uintptr_t heap_max;     // maximum heap (set to whatever you like)

static kmem_block_t *free_list = NULL;

// Round `bytes` up to a multiple of FRAME_SIZE
static inline uint32_t round_up_frame(uint32_t bytes) {
    return (bytes + FRAME_SIZE - 1) & ~(FRAME_SIZE - 1);
}

// Grow the heap by at least `bytes` (rounded up to page granularity).
// Returns pointer to the newly added region (phys==virt), or NULL.
static void *kbrk(uint32_t bytes) {
    uint32_t want = round_up_frame(bytes);
    if (heap_end + want > heap_max)
        return NULL;    // no more heap room

    // allocate pages
    for (uintptr_t addr = heap_end;
         addr < heap_end + want;
         addr += FRAME_SIZE)
    {
        void *p = pmm_alloc_frame();
        if (!p)
        {
            printf("kbrk: OOM\n");
            return NULL;    // OOM
        }
        // identity-mapped, so nothing else needed
    }

    // carve out a new free block for the region
    kmem_block_t *block = (kmem_block_t*)heap_end;
    block->size = want;
    block->next = free_list;
    free_list = block;

    uintptr_t old = heap_end;
    heap_end += want;
    return (void*)old;
}

void *kmalloc(size_t n) {
    // allocate header + data, align header to 8 bytes
    uint32_t total = (uint32_t)n + sizeof(kmem_block_t);
    total = (total + 7) & ~7;

    kmem_block_t **prev = &free_list;
    kmem_block_t  *b    = free_list;

    // first-fit search
    while (b) {
        if (b->size >= total) {
            // if leftover is big enough to hold a new block header, split
            if (b->size >= total + sizeof(kmem_block_t) + 8) {
                kmem_block_t *nb = (kmem_block_t*)(((uintptr_t)b) + total);
                nb->size = b->size - total;
                nb->next = b->next;
                b->size  = total;
                *prev    = nb;
            } else {
                // take the entire block
                *prev = b->next;
            }
            // return address just after header
            return (void*)(((uintptr_t)b) + sizeof(kmem_block_t));
        }
        prev = &b->next;
        b = b->next;
    }

    // printf("kmalloc: no free block found, growing heap\n");

    // no fitting block—grow heap and retry once
    if (!kbrk(total * 2))
        return NULL;    // OOM
    return kmalloc(n);
}

void kfree(void *ptr) {
    if (!ptr) return;
    kmem_block_t *b = (kmem_block_t*)(((uintptr_t)ptr) - sizeof(kmem_block_t));
    // simply push onto free list; coalescing left as an exercise
    b->next = free_list;
    free_list = b;
}

size_t ksize(void *ptr) {
    if (!ptr) return 0;
    kmem_block_t *b = (kmem_block_t*)(((uintptr_t)ptr) - sizeof(kmem_block_t));
    return b->size - sizeof(kmem_block_t);
}

// Call this early, from your kernel_init or main, after pmm_init()
// Set heap_start and heap_max here:
void kmem_init(void) {
    uintptr_t ek = (uintptr_t)&endkernel;
    heap_start = (ek + FRAME_SIZE - 1) & ~(FRAME_SIZE - 1);
    heap_end   = heap_start;
    // choose heap_max = e.g. 32 MiB past kernel, or next reserved region
    heap_max   = heap_start + (32 * 1024 * 1024);

    free_list = NULL;
    printf("Heap start: %x\n", heap_start);
    printf("Heap end: %x\n", heap_end);
    printf("Heap max: %x\n", heap_max);
    printf("Heap size: %x\n", heap_max - heap_start);
    install_all_cmds(global_commands, GLOBAL);
}

void m_test_kmem()
{
    char *p1 = kmalloc(0x100);
    printf("Allocated: %d\n", ksize(p1));
    char *p2 = kmalloc(0x200);
    char *p3 = kmalloc(0x300);
    char *p4 = kmalloc(0x400);
    char *p5 = kmalloc(0x500);
    char *p6 = kmalloc(0x600);
    char *p7 = kmalloc(0x700);
    char *p8 = kmalloc(0x800);

    printf("Allocated: %x %x %x %x %x %x %x %x\n", p1, p2, p3, p4, p5, p6, p7, p8);

    kfree(p1);
    kfree(p2);
    kfree(p3);
    kfree(p4);
    kfree(p5);
    kfree(p6);
    kfree(p7);
    kfree(p8);

}
