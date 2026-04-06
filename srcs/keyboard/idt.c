#include "idt.h"

idt_entry idt[256];
idt_ptr idtp;

/* Create handlers */
void idt_set_gate(int idx, uint32_t base)
{
    unsigned short cs;
    __asm__ ("mov %%cs, %0" : "=r" (cs));

    idt[idx].offset_low = LOW_16(base);
    idt[idx].offset_high = HIGH_16(base);
    idt[idx].selector = cs;
    idt[idx].zero = 0;
    idt[idx].type_attr = 0x8E;
}

void idt_set_gate_user(int idx, uint32_t base)
{
    unsigned short cs;
    __asm__ ("mov %%cs, %0" : "=r" (cs));

    idt[idx].offset_low = LOW_16(base);
    idt[idx].offset_high = HIGH_16(base);
    idt[idx].selector = cs;
    idt[idx].zero = 0;
    idt[idx].type_attr = 0xEE; /* P=1, DPL=3, S=0, Type=1110 (32-bit interrupt gate) */
}

void register_idt()
{
	idtp.base = (uint32_t) &idt;
	idtp.limit = (uint16_t) (256 * sizeof(idt) - 1);
	__asm__ __volatile__("lidtl (%0)" : : "r" (&idtp));
}
