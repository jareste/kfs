#include "gdt.h"
#include "../display/display.h"

gdt_entry_t gdt[GDT_ENTRIES];
gdt_ptr_t* gdt_ptr = (gdt_ptr_t*)GDT_ADDRESS;

typedef struct __attribute__((packed)) tss_entry
{
    uint32_t prevTss;
    uint32_t esp0;
    uint32_t ss0;
    uint32_t esp1;
    uint32_t ss1;
    uint32_t esp2;
    uint32_t ss2;
    uint32_t cr3;
    uint32_t eip;
    uint32_t eflags;
    uint32_t eax;
    uint32_t ecx;
    uint32_t edx;
    uint32_t ebx;
    uint32_t esp;
    uint32_t ebp;
    uint32_t esi;
    uint32_t edi;
    uint32_t es;
    uint32_t cs;
    uint32_t ss;
    uint32_t ds;
    uint32_t fs;
    uint32_t gs;
    uint32_t ldt;
    uint16_t trap;
    uint16_t iomap;
} tss_entry_t;

static tss_entry_t tss;

extern void load_tss();
#include "../memory/memory.h"

// void tss_init()
// {
//     memset(&tss, 0, sizeof(tss_entry_t));
//     tss.ss0 = 0x10;
//     tss.esp0 = (uint32_t)kmalloc(KB(4));
//     ASSERT(tss.esp0);
//     tss.iomap = sizeof(tss_entry_t);
//     printf("TSS ESP0: %p\n", tss.esp0);

//     load_tss();
// }

void tss_init()
{
    memset(&tss, 0, sizeof(tss_entry_t));
    uint32_t *stack = kmalloc(KB(4));
    stack += KB(4) / sizeof(uint32_t);
    tss.esp0 = (uint32_t)stack & 0xFFFFFFF0;
    tss.ss0 = 0x10;
    tss.iomap = sizeof(tss_entry_t);
    printf("TSS ESP0: %p\n", tss.esp0);
    load_tss();
}

/* Assembly function to load the GDT */
extern void gdt_flush();

void register_gdt(void)
{
    gdt_ptr->base = (uint32_t) &gdt;
    gdt_ptr->limit = (GDT_ENTRIES * sizeof(gdt_entry_t)) - 1;
    __asm__ __volatile__("lgdtl (%0)" : : "r" (gdt_ptr));
    gdt_flush();
}

void gdt_set_entry(int index, uint32_t base, uint32_t limit, uint8_t access, uint8_t granularity)
{
    gdt[index].base_low = (base & 0xFFFF);
    gdt[index].base_middle = (base >> 16) & 0xFF;
    gdt[index].base_high = (base >> 24) & 0xFF;
    gdt[index].limit_low = (limit & 0xFFFF);
    gdt[index].granularity = (limit >> 16) & 0x0F;
    gdt[index].granularity |= (granularity & 0xF0);
    gdt[index].access = access;
}

void gdt_init()
{
    /* NULL */
    gdt_set_entry(0, 0, 0, 0, 0);

    /* Kernel Code segment */
    gdt_set_entry(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);

    /* Kernel Data segment */
    gdt_set_entry(2, 0, 0xFFFFFFFF, 0x92, 0xCF);

    /* Kernel Stack */
    gdt_set_entry(3, 0, 0xFFFFFFFF, 0x96, 0xCF);

    /* User mode code segment */
    gdt_set_entry(4, 0, 0xFFFFFFFF, 0xFA, 0xCF);

    /* User mode data segment */
    gdt_set_entry(5, 0, 0xFFFFFFFF, 0xF2, 0xCF);

    /* User mode stack */
    gdt_set_entry(6, 0, 0xFFFFFFFF, 0xF6, 0xCF);

    /* task state segment */
    gdt_set_entry(7, (uint32_t)&tss, sizeof(tss) - 1, 0x89, 0x40);

    register_gdt();
    tss_init();
}
