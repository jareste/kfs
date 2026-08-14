#include "gdt.h"
#include "../display/display.h"
#include "../tasks/task.h"

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
#include "../memory/kmalloc.h"

void tss_init()
{
    memset(&tss, 0, sizeof(tss_entry_t));
    uint32_t *stack = kmalloc(KB(4));
    tss.esp0 = ((uint32_t)stack + KB(4)) & 0xFFFFFFF0;
    tss.ss0 = 0x10;
    tss.iomap = sizeof(tss_entry_t);
    load_tss();
}

void tss_set_stack(uint32_t stack)
{
    tss.esp0 = stack;
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

void print_gdt8()
{
    uint32_t tls_base = gdt[8].base_low |
                        ((uint32_t)gdt[8].base_middle << 16) | 
                        ((uint32_t)gdt[8].base_high << 24);
    kprintf("GDT[8] base=%x access=%x\n", tls_base, gdt[8].access);
    kprintf("current_task->tls_base=%x\n", get_current_task()->tls_base);

    uint32_t limit = gdt[8].limit_low | 
                    (((uint32_t)gdt[8].granularity & 0x0F) << 16);
    uint32_t gran = gdt[8].granularity >> 4;
    kprintf("GDT[8] limit=%x gran=%x\n", limit, gran);
    uint32_t gs_zero;
    __asm__ volatile(
        "push %%gs\n"
        "mov %1, %%gs\n"
        "mov %%gs:0x0, %0\n"
        "pop %%gs\n"
        : "=r"(gs_zero)
        : "r"((uint32_t)0x43)
    );
    kprintf("gs:0x0 = %x\n", gs_zero);
    uint32_t tls_va = 0x804d000;
    uint32_t di = tls_va >> 22;
    uint32_t ti = (tls_va >> 12) & 0x3FF;
    page_directory_t *dir = get_current_task()->page_dir;
    uint32_t pde = dir->entries[di];
    page_table_t *tbl = (page_table_t*)(pde & 0xFFFFF000);
    uint32_t pte = tbl->entries[ti];
    kprintf("PDE=%x PTE=%x\n", pde, pte);
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

    /* TLS - to be filled on set_thread_area */
    gdt_set_entry(8, 0, 0, 0, 0);

    register_gdt();
}
