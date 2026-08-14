#ifndef IO_H
#define IO_H

#include "../utils/stdint.h"
#include "../keyboard/idt.h"

void outb(uint16_t port, uint8_t data);
uint8_t inb(uint16_t port);
uint16_t inw(uint16_t port);
void outw(uint16_t port, uint16_t data);

void insw(uint16_t port, void *addr, uint32_t count);
void outsw(uint16_t port, const void *addr, uint32_t count);

#endif