#include "display/display.h"
#include "keyboard/idt.h"
#include "gdt/gdt.h"
#include "kshell/kshell.h"
#include "timers/timers.h"
#include "memory/memory.h"
#include "keyboard/signals.h"
#include "tasks/task.h"
#include "ide/ide.h"
#include "ide/ext2.h"

#include "umgmnt/users.h"

extern uint32_t endkernel;

void kernel_main()
{
    clear_screen();
    init_kshell();

    paging_init();
    init_interrupts();
    heap_init();
    gdt_init();

    init_timer();

    enable_interrupts();

    ide_demo();

    ext2_mount();
    ext2_demo();

    init_users_api();

    list_users();

    // kshell(); /* Uncomment this line to not run the scheduler */
    scheduler_init();
    start_foo_tasks();
    scheduler();

    /* Keep CPU busy */
    while (1)
    {
    }
    
}
