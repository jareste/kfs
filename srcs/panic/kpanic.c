#include "../display/display.h"

extern void clear_registers();

void __kpanic(char* msg, int fatal, const char* file, int line, const char* func_name)
{
    if (fatal)
    {
        set_putchar_color(RED);
        kprintf("\n[KERNEL PANIC] %s\n", msg);
        kprintf("Panic on:\n\tfile: '%s'\n\tline: '%d'\n\tfunc: '%s'\n\tmsg:  '%s'\n",\
            file, line, func_name, msg);
        kprintf("System halted.\n");

        /* Useful debug information */
        // dump_registers();
        clear_registers();
        
        /* Disable interrupts and halt forever */
        __asm__ __volatile__(
            "cli\n"
        ".kpanic_halt:\n"
            "hlt\n"
            "jmp .kpanic_halt\n"
        );
    }
    else
    {
        kprintf("\n[KERNEL WARNING] %s\n", msg);
        /* Non-fatal: print and return to caller */
    }
}
