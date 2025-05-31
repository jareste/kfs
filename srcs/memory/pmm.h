#ifndef PMM_H
#define PMM_H

#include "../utils/stdint.h"

#define FRAME_SIZE 0x1000

void pmm_init(uint32_t count, uint32_t map_ptr);
void *pmm_alloc_frame(void);
uint32_t allocate_frame();
void free_frame(uint32_t phys_addr);

#endif