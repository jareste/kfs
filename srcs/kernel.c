#include "display/display.h"
#include "keyboard/idt.h"
#include "gdt/gdt.h"
#include "kshell/kshell.h"
#include "time/time.h"
#include "memory/memory.h"
#include "keyboard/signals.h"
#include "tasks/task.h"
#include "ide/ide.h"
#include "ide/ext2.h"
#include "syscalls/syscalls.h"
#include "modules/modules.h"

#include "umgmnt/users.h"

extern uint32_t endkernel;

void kernel_main()
{
    disable_print();
    clear_screen();
    init_kshell();

    paging_init();
    init_interrupts();
    heap_init();
    gdt_init();

    init_timer();

    enable_interrupts();

    // ide_demo();

    ext2_mount();

    init_users_api();

    list_users();

    init_syscalls();


    tty_init();
    // kshell(); /* Uncomment this line to not run the scheduler */
    scheduler_init();
    start_foo_tasks();

    enable_print();
    register_time_module();
    register_keyboard_module();


    scheduler();

    /* Keep CPU busy */
    while (1)
    {
    }
    
}
