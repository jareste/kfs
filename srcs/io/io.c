#include "../utils/stdint.h"

void outw(uint16_t port, uint16_t data)
{
    __asm__ __volatile__("outw %0, %1" : : "a"(data), "Nd"(port));
}

void outb(uint16_t port, uint8_t data)
{
	__asm__("out %%al, %%dx" : :"a"(data), "d"(port));
}

uint8_t inb(uint16_t port)
{
   uint8_t ret;
   asm volatile ("inb %%dx,%%al":"=a" (ret):"d" (port));
   return ret;
}

uint16_t inw(uint16_t port)
{
    uint16_t ret;
    __asm__ __volatile__("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

void insw(uint16_t port, void *addr, uint32_t count)
{
    __asm__ __volatile__("cld; rep insw" : "+D"(addr), "+c"(count) : "d"(port) : "memory");
}

void outsw(uint16_t port, const void *addr, uint32_t count)
{
    __asm__ __volatile__("cld; rep outsw" : "+S"(addr), "+c"(count) : "d"(port) : "memory");
}
