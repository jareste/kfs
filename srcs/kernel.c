#include "boot/multiboot.h"
#include "display/display.h"
#include "keyboard/idt.h"
#include "gdt/gdt.h"
#include "kshell/kshell.h"
#include "time/time.h"
#include "memory/pmm.h"
#include "memory/vmm.h"
#include "panic/kpanic.h"
#include "keyboard/signals.h"
#include "tasks/task.h"
#include "ide/ide.h"
#include "ide/ext2.h"
#include "syscalls/syscalls.h"
#include "modules/modules.h"

#include "umgmnt/users.h"

extern uint32_t endkernel;
void set_run_scheduler(int value);
void kernel_main(uint32_t magic, multiboot_info_t *mbi)
{
    // gdt_init();
    // disable_print();
    // clear_screen();

    init_kshell();

    // paging_init();
    // init_interrupts();
    // heap_init();
    gdt_init();

    if (magic != MULTIBOOT_MAGIC)
        kpanic("kernel_main: invalid multiboot magic", 1);

    pmm_init(mbi);

    /* 4. Virtual memory manager / paging ---------------------------- */
    vmm_init();

    kmalloc_init(0);    /* 0 = auto-place after kernel + PMM bitmap   */

    /* 7. Virtual allocator ------------------------------------------ */
    init_interrupts();
    vmalloc_init();

    init_timer();
    // kpanic("Kernel panic: reached end of kernel_main", 1);
    tss_init();


    enable_interrupts();

    // ide_demo();

    ext2_mount();

    init_users_api();

    list_users();

    init_syscalls();

    // int vmalloc_size = 100; /* intentionally not page-aligned to test rounding */
    // void* ptr = vmalloc(MB(vmalloc_size) + 1); /* intentionally not page-aligned to test rounding */
    // /* write check */
    // int max_loops = vmalloc_size * 1024 * 1024 / 4096;
    // for (int i = 23000; i < max_loops; i++)
    // {
    //     ((uint32_t*)ptr)[i] = i;
    //     kprintf("Wrote %d to %x\n", i, (uint32_t)ptr + i * 4096);
    // }
    // /* read check */
    // for (int i = 23000; i < max_loops; i++)
    // {
    //     uint32_t val = ((uint32_t*)ptr)[i];
    //     if (val != i)
    //         kpanic("vmalloc read/write check failed", 1);
    //     kprintf("Read %d from %x\n", val, (uint32_t)ptr + i * 4096);
    // }
    // vfree(ptr);

    // tty_init();
    // kshell(); /* Uncomment this line to not run the scheduler */
    scheduler_init();

    enable_print();
    ext2_remove_all_files("/dev");
    register_time_module();
    register_keyboard_module();

    enable_interrupts();
    outb(0x20, 0x20);
    start_foo_tasks();
    set_run_scheduler(1);

    /* Keep CPU busy */
    while (1)
    {
        __asm__ __volatile__("hlt");
    }
    
}
