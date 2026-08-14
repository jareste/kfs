#ifndef IDT_H
#define IDT_H

#include "../utils/stdint.h"

#define PIC1_COMMAND 0x20
#define PIC_EOI 0x20

/* https://wiki.osdev.org/Interrupt_Descriptor_Table */
/* https://samypesse.gitbook.io/how-to-create-an-operating-system/chapter-7 */
typedef struct __attribute__((packed)) idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t zero;
    uint8_t type_attr;
    uint16_t offset_high;
} idt_entry;

typedef struct __attribute__((packed)) idt_ptr {
    uint16_t limit;
    uint32_t base;
} idt_ptr;

typedef struct __attribute__((packed)) registers {
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t esp;
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;
} registers;

typedef struct __attribute__((packed)) error_state {
    uint32_t err_code;
    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;
} error_state;

typedef struct __attribute__((packed)) error_state_user {
    uint32_t err_code;
    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;
    uint32_t esp_user;
    uint32_t ss_user;
} error_state_user_t;

extern idt_entry idt[256];
extern idt_ptr idtp;

void enable_interrupts(void);
void disable_interrupts(void);

void init_interrupts();

void idt_set_gate(int idx, uint32_t base);
void idt_set_gate_user(int idx, uint32_t base);
void register_idt();

void init_page_signals();

extern void isr_handler_0();
extern void isr_handler_1();
extern void isr_handler_2();
extern void isr_handler_3();
extern void isr_handler_4();
extern void isr_handler_5();
extern void isr_handler_6();
extern void isr_handler_7();
extern void isr_handler_8();
extern void isr_handler_9();
extern void isr_handler_10();
extern void isr_handler_11();
extern void isr_handler_12();
extern void isr_handler_13();
extern void isr_handler_14();
extern void isr_handler_15();
extern void isr_handler_16();
extern void isr_handler_17();
extern void isr_handler_18();
extern void isr_handler_19();
extern void isr_handler_20();
extern void isr_handler_21();
extern void isr_handler_22();
extern void isr_handler_23();
extern void isr_handler_24();
extern void isr_handler_25();
extern void isr_handler_26();
extern void isr_handler_27();
extern void isr_handler_28();
extern void isr_handler_29();
extern void isr_handler_30();
extern void isr_handler_31();

// array of irq handler functs
extern void irq_handler_0();
extern void irq_handler_1();
extern void irq_handler_2();
extern void irq_handler_3();
extern void irq_handler_4();
extern void irq_handler_5();
extern void irq_handler_6();
extern void irq_handler_7();
extern void irq_handler_8();
extern void irq_handler_9();
extern void irq_handler_10();
extern void irq_handler_11();
extern void irq_handler_12();
extern void irq_handler_13();
extern void irq_handler_14();
extern void irq_handler_15();

extern void syscall_handler_asm();

#endif
