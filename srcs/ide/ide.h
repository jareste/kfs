#ifndef IDE_H
#define IDE_H

#include "../utils/stdint.h"

#define IDE_DATA        0x1F0
#define IDE_ERROR       0x1F1
#define IDE_SECT_COUNT  0x1F2
#define IDE_LBA_LOW     0x1F3
#define IDE_LBA_MID     0x1F4
#define IDE_LBA_HIGH    0x1F5
#define IDE_DRIVE_SEL   0x1F6
#define IDE_STATUS      0x1F7
#define IDE_CMD         0x1F7
#define IDE_DEV_CTRL    0x3F6

#define IDE_STATUS_ERR  (1 << 0)
#define IDE_STATUS_DRQ  (1 << 3)
#define IDE_STATUS_BSY  (1 << 7)

#define IDE_CMD_READ    0x20
#define IDE_CMD_WRITE   0x30

#define IDE_SECTOR_SIZE 512

void ide_init();
void ide_read_sector_monotask(uint32_t lba, uint16_t* buffer);
void ide_write_sector_monotask(uint32_t lba, uint16_t* buffer);
void ide_read_sectors_monotask(uint32_t lba, uint8_t count, uint16_t* buffer);
void ide_write_sectors_monotask(uint32_t lba, uint8_t count, uint16_t* buffer);
int  ide_read_sectors(uint32_t lba, uint8_t count, void *buffer);
int  ide_write_sectors(uint32_t lba, uint8_t count, void *buffer);
void ide_irq_handler();
void ide_demo();

#endif