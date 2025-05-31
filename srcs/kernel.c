#include "display/display.h"
#include "keyboard/idt.h"
#include "gdt/gdt.h"
#include "kshell/kshell.h"
#include "timers/timers.h"
#include "memory/kmem.h"
#include "memory/pmm.h"
#include "keyboard/signals.h"

extern uint32_t endkernel;

#include "memory/multiboot.h"


void kernel_main(uint32_t magic, uint32_t mbi_addr)
{
    clear_screen();
    init_kshell();

    gdt_init();

    init_interrupts();

    if (magic != MULTIBOOT_BOOTLOADER_MAGIC) {
        puts_color("Invalid magic number\n", LIGHT_RED);
        // Hang, blink, print error… whatever you like
        for (;;) __asm__("hlt");
    }

    multiboot_info_t *mbi = (multiboot_info_t *)(uintptr_t)mbi_addr;

    if (!(mbi->flags & (1 << 6))) {
        // Bit 6 means “memory map provided”
        for (;;) __asm__("hlt");
    }

    /* Call your existing physical-memory init: */
    pmm_init(mbi->mmap_addr, mbi->mmap_length);
    kmem_init();

    // paging_init();

    init_timer();

    enable_interrupts();
    init_signals();

    // heap_init();

    kshell();
    printf("Exiting shell...\n");
    /* Keep CPU busy */
    while (1)
    {
    }
}
