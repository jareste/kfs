#include "display/display.h"
#include "keyboard/idt.h"
#include "gdt/gdt.h"

#define BANNER "Welcome to 42Barcelona's jareste- OS\n"

void kernel_main()
{
    clear_screen();

    gdt_init();

    init_interrupts();

    puts_color(BANNER, LIGHT_GREEN);


    enable_interrupts();

    /* Keep CPU busy */
    while (1)
    {
    }
}
