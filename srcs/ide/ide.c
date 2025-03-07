#include "ide.h"
#include "../io/io.h"
#include "../utils/utils.h"
#include "../memory/memory.h"
#include "../display/display.h"
#include "../tasks/task.h"

#define MAX_LBA 0x0FFFFFFF

#define le16_to_cpu(x) ((x) >> 8) | ((x) << 8)
#define le32_to_cpu(x) ((x) >> 24) | (((x) & 0xFF0000) >> 8) | (((x) & 0xFF00) << 8) | ((x) << 24)

/* TODO move semaphores to it's own file */
#define atomic_load(ptr) __atomic_load_n(ptr, __ATOMIC_SEQ_CST)
#define atomic_store(ptr, val) __atomic_store_n(ptr, val, __ATOMIC_SEQ_CST)

typedef struct
{
    volatile int value;
} semaphore;

static semaphore ide_sem = {0};

static void sema_wait(semaphore* sem)
{
    while (1)
    {
        if (atomic_load(&sem->value))
        {
            atomic_store(&sem->value, 0);
            break;
        }
        asm volatile("sti; hlt; cli");
        // scheduler();
    }
}

static void sema_signal(semaphore* sem)
{
    atomic_store(&sem->value, 1);
}

void ide_irq_handler()
{
    inb(IDE_STATUS);
    sema_signal(&ide_sem);
}

void ide_init()
{
    outb(IDE_DEV_CTRL, 0x00);
}

static void ide_wait_nonbusy()
{
    while (inb(IDE_STATUS) & IDE_STATUS_BSY);
}

static void ide_select_drive(uint32_t lba)
{
    outb(IDE_DRIVE_SEL, 0xE0 | ((lba >> 24) & 0x0F));
}

void ide_read_sector(uint32_t lba, uint16_t* buffer)
{
    uint8_t status = inb(IDE_STATUS);
    disable_interrupts();
    
    ide_wait_nonbusy();
    ide_select_drive(lba);
    
    outb(IDE_SECT_COUNT, 1);
    outb(IDE_LBA_LOW, lba & 0xFF);
    outb(IDE_LBA_MID, (lba >> 8) & 0xFF);
    outb(IDE_LBA_HIGH, (lba >> 16) & 0xFF);
    outb(IDE_CMD, IDE_CMD_READ);
    
    enable_interrupts();
    sema_wait(&ide_sem);
    disable_interrupts();

    if (inb(IDE_STATUS) & IDE_STATUS_ERR)
    {
        kernel_panic("IDE read error");
    }

    for (int i = 0; i < 256; i++)
    {
        // uint16_t word = inw(IDE_DATA);
        // buffer[i] = (word << 8) | (word >> 8);
        buffer[i] = inw(IDE_DATA);
    }
    
    enable_interrupts();
    status = inb(IDE_STATUS);
}

int ide_read_sectors(uint32_t lba, uint8_t count, void* buffer)
{
    if (lba > MAX_LBA || count == 0 || count > 256) return -1;
    
    uint16_t* buf = (uint16_t*)buffer;
    for (uint8_t i = 0; i < count; i++)
    {
        ide_read_sector(lba + i, buf + (i * IDE_SECTOR_SIZE/2));
    }
    return 0;
}

void ide_write_sector(uint32_t lba, uint16_t* buffer)
{
    disable_interrupts();
    
    ide_wait_nonbusy();
    ide_select_drive(lba);
    
    outb(IDE_SECT_COUNT, 1);
    outb(IDE_LBA_LOW, lba & 0xFF);
    outb(IDE_LBA_MID, (lba >> 8) & 0xFF);
    outb(IDE_LBA_HIGH, (lba >> 16) & 0xFF);
    outb(IDE_CMD, IDE_CMD_WRITE);
    
    for (int i = 0; i < 256; i++)
    {
        outw(IDE_DATA, buffer[i]);
    }
    
    enable_interrupts();
    sema_wait(&ide_sem);
    disable_interrupts();

    if (inb(IDE_STATUS) & IDE_STATUS_ERR)
    {
        kernel_panic("IDE write error");
    }
    
    enable_interrupts();
}

int ide_write_sectors(uint32_t lba, uint8_t count, void* buffer)
{
    if (lba > MAX_LBA || count == 0 || count > 256) return -1;
    
    uint16_t* buf = (uint16_t*)buffer;
    for (uint8_t i = 0; i < count; i++)
    {
        ide_write_sector(lba + i, buf + (i * IDE_SECTOR_SIZE/2));
    }
    return 0;
}

void ide_flush()
{
    disable_interrupts();
    ide_wait_nonbusy();
    outb(IDE_CMD, 0xE7);
    enable_interrupts();
    sema_wait(&ide_sem);
}

#include "../memory/memory.h"
#include "../utils/utils.h"

void ide_demo()
{
    uint16_t* read_buffer = kmalloc(512);
    uint16_t* write_buffer = kmalloc(512);
    
    memset(read_buffer, 0, 512);
    memset(write_buffer, 0, 512);

    for(int i = 0; i < 256; i++)
    {
        write_buffer[i] = 0xF00D0000 | (i & 0xFFFF);
    }

    ide_read_sector(0, read_buffer);
    
    // printf("MBR Signature: ");
    // for(int i = 0; i < 16; i++)
    // {
    //     printf("%x ", ((uint8_t*)read_buffer)[i]);
    // }
    // printf("\n");
    
    if(read_buffer[255] == 0xAA55) /* Boot signature */
    {
        printf("Valid MBR boot signature\n");
    }

    for(int i = 0; i < 256; i++)
    {
        write_buffer[i] = (i << 8) | (i & 0xFF);
    }

    ide_write_sector(1000, write_buffer);

    ide_flush();
    
    ide_read_sector(1000, read_buffer);
    
    if(memcmp(write_buffer, read_buffer, 512))
    {
        printf("Write verification failed!\n");
    }
    else
    {
        printf("Write verification successful!\n");
    }

    kfree(read_buffer);
    kfree(write_buffer);
}
