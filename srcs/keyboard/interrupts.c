#include "../utils/stdint.h"
#include "../display/display.h"
#include "../io/io.h"
#include "../time/time.h"
#include "idt.h"
#include "../memory/memory.h"
#include "keyboard.h"
#include "signals.h"
#include "../syscalls/syscalls.h"
#include "../tasks/task.h"
#include "../ide/ide.h"

void enable_interrupts(void)
{
	__asm__ __volatile__("sti");
}

void disable_interrupts(void)
{
	__asm__ __volatile__("cli");
}

/* PAGE FAULT HANDLER */
void page_fault_handler(registers* regs, error_state* stack)
{
    uint32_t faulting_address;
    __asm__ __volatile__("mov %%cr2, %0" : "=r"(faulting_address));

    task_t *task = get_current_task();
    if (task)
    {
        printf("Task(%d) '%s': ", task->pid, task->name);
    }
    printf("Page fault at 0x");
    put_hex(faulting_address);
    putc('\n');
    printf("Error code: 0x");
    put_hex(stack->err_code);
    putc('\n');

    printf("Error type: ");
    if (!(stack->err_code & 0x1)) printf("Non-present page ");
    if (stack->err_code & 0x2) printf("Write ");
    else printf("Read ");
    if (stack->err_code & 0x4) printf("User-mode\n");
    else printf("Kernel-mode\n");

    printf("Kernel uptime: %d\n", get_kuptime());


    printf("Killing task %d due to page fault.\n", task->pid);
    _exit(-1);  // or call kill_task() as appropriate
}
/* PAGE FAULT HANDLER */

void isr_handler(registers reg, uint32_t intr_no, uint32_t err_code, error_state stack)
{
	UNUSED(reg)
    UNUSED(stack)
    UNUSED(err_code)
    UNUSED(intr_no)

    // printf("Interrupt SW number: %d\n", intr_no);

    if (intr_no == 14 || intr_no == 13)
    {
        page_fault_handler(&reg, &stack);
    }

    _kill(0, intr_no); /* kernell side so call _kill directly */
    
    // while(1);
    if (intr_no >= 32)
    {
        outb(PIC_EOI, PIC1_COMMAND);
    }

}

void irq_handler(registers reg, uint32_t intr_no, uint32_t err_code, error_state stack)
{
	(void) err_code;
    (void) stack;
    (void) reg;

    // static int timer_ticks = 0;
    // timer_ticks++;

    // if (timer_ticks % 200 == 0)
    // {
    //     timer_ticks = 0;
    //     // printf("Switching tasks\n");
    // }

    switch (intr_no)
    {
        case 0:
            // scheduler();
            // disable_interrupts();
            irq_handler_timer();
            break;
        case 1:
            keyboard_handler();
            break;
        case 14:
            ide_irq_handler();
            break;
        default:
        printf("eax: %d\n", reg.eax);
        printf("ebx: %d\n", reg.ebx);
            printf("Interrupt HW number: %d\n", intr_no);
            kernel_panic("Unknown interrupt");
    }

    // enable_interrupts();
    // if (intr_no >= 8)
    // puts("#######################################################");
    if(intr_no >= 8) outb(0xA0, 0x20);
	outb(PIC_EOI, PIC1_COMMAND);
}

void init_interrupts()
{
    idt_set_gate(0, (uint32_t)isr_handler_0); /* Division by zero */
    idt_set_gate(1, (uint32_t)isr_handler_1); /* Debug */
    idt_set_gate(2, (uint32_t)isr_handler_2); /* Non-maskable interrupt */
    idt_set_gate(3, (uint32_t)isr_handler_3); /* Breakpoint */
    idt_set_gate(4, (uint32_t)isr_handler_4); /* Overflow */
    idt_set_gate(5, (uint32_t)isr_handler_5); /* Bound range exceeded */
    idt_set_gate(6, (uint32_t)isr_handler_6); /* Invalid opcode */
    idt_set_gate(7, (uint32_t)isr_handler_7); /* Device not available */
    idt_set_gate(8, (uint32_t)isr_handler_8); /* Double fault */
    idt_set_gate(9, (uint32_t)isr_handler_9); /* Coprocessor segment overrun */
    idt_set_gate(10, (uint32_t)isr_handler_10); /* Invalid TSS */
    idt_set_gate(11, (uint32_t)isr_handler_11); /* Segment not present */
    idt_set_gate(12, (uint32_t)isr_handler_12); /* Stack-segment fault */
    idt_set_gate(13, (uint32_t)isr_handler_13); /* General protection fault */
    idt_set_gate(14, (uint32_t)isr_handler_14); /* Page fault */
    idt_set_gate(15, (uint32_t)isr_handler_15); /* Unknown interrupt */
    idt_set_gate(16, (uint32_t)isr_handler_16); /* Coprocessor fault */
    idt_set_gate(17, (uint32_t)isr_handler_17); /* Alignment check */
    idt_set_gate(18, (uint32_t)isr_handler_18); /* Machine check */
    idt_set_gate(19, (uint32_t)isr_handler_19); /* SIMD floating-point exception */
    idt_set_gate(20, (uint32_t)isr_handler_20); /* Virtualization exception */
    idt_set_gate(21, (uint32_t)isr_handler_21); /* Control protection exception */
    idt_set_gate(22, (uint32_t)isr_handler_22); /* Unknown interrupt */
    idt_set_gate(23, (uint32_t)isr_handler_23); /* Unknown interrupt */
    idt_set_gate(24, (uint32_t)isr_handler_24); /* Unknown interrupt */
    idt_set_gate(25, (uint32_t)isr_handler_25); /* Unknown interrupt */
    idt_set_gate(26, (uint32_t)isr_handler_26); /* Unknown interrupt */
    idt_set_gate(27, (uint32_t)isr_handler_27); /* Unknown interrupt */
    idt_set_gate(28, (uint32_t)isr_handler_28); /* Unknown interrupt */
    idt_set_gate(29, (uint32_t)isr_handler_29); /* Unknown interrupt */
    idt_set_gate(30, (uint32_t)isr_handler_30); /* Unknown interrupt */
    idt_set_gate(31, (uint32_t)isr_handler_31); /* Unknown interrupt */

    /* remap PIC */

    unsigned char a1 = inb(0x21);
    unsigned char a2 = inb(0xA1);

    outb(0x20, 0x11);
    outb(0xA0, 0x11);
    outb(0x21, 0x20);
    outb(0xA1, 0x28);
    outb(0x21, 0x04);
    outb(0xA1, 0x02);
    outb(0x21, 0x01);
    outb(0xA1, 0x01);
    outb(0x21, a1);
    outb(0xA1, a2);

    outb(0x21, inb(0x21) & ~0x01); // Clear the mask for IRQ0
    outb(0xA1, inb(0xA1) & ~(1 << 6)); // Clear the mask for IRQ14

    idt_set_gate(32, (uint32_t)irq_handler_0); /* Programmable Interrupt Timer Interrupt */
    idt_set_gate(33, (uint32_t)irq_handler_1); /* Keyboard */
    idt_set_gate(34, (uint32_t)irq_handler_2); /* Cascade (Never raised) */
    idt_set_gate(35, (uint32_t)irq_handler_3); /* COM2 */
    idt_set_gate(36, (uint32_t)irq_handler_4); /* COM1 */
    idt_set_gate(37, (uint32_t)irq_handler_5); /* LPT2 */
    idt_set_gate(38, (uint32_t)irq_handler_6); /* Floppy Disk */
    idt_set_gate(39, (uint32_t)irq_handler_7); /* LPT1 */
    idt_set_gate(40, (uint32_t)irq_handler_8); /* CMOS real-time clock */
    idt_set_gate(41, (uint32_t)irq_handler_9); /* Free for peripherals / legacy SCSI / NIC */
    idt_set_gate(42, (uint32_t)irq_handler_10); /* Free for peripherals / SCSI / NIC */
    idt_set_gate(43, (uint32_t)irq_handler_11); /* Free for peripherals / SCSI / NIC */
    idt_set_gate(44, (uint32_t)irq_handler_12); /* PS/2 Mouse */
    idt_set_gate(45, (uint32_t)irq_handler_13); /* FPU / Coprocessor / Inter-processor */
    idt_set_gate(46, (uint32_t)irq_handler_14); /* Primary ATA Hard Disk */
    idt_set_gate(47, (uint32_t)irq_handler_15); /* Secondary ATA Hard Disk */

    idt_set_gate(0x30, (uint32_t)syscall_handler_asm);

    register_idt();
    ide_init();
}
