
#include "ide.h"
#include "../io/io.h"
#include "../utils/utils.h"
#include "../memory/memory.h"
#include "../display/display.h"
#include "../tasks/task.h"
#include "../panic/kpanic.h"
#include "../keyboard/idt.h"

#define MAX_LBA        0x0FFFFFFF

#define le16_to_cpu(x) ((x) >> 8) | ((x) << 8)
#define le32_to_cpu(x) ((x) >> 24) | (((x) & 0xFF0000) >> 8) | (((x) & 0xFF00) << 8) | ((x) << 24)

/* TODO move semaphores to it's own file */
#define atomic_load(ptr) __atomic_load_n(ptr, __ATOMIC_SEQ_CST)
#define atomic_store(ptr, val) __atomic_store_n(ptr, val, __ATOMIC_SEQ_CST)

typedef struct
{
    volatile int value;
} semaphore;

typedef enum
{
    IDE_REQ_READ,
    IDE_REQ_WRITE,
} ide_req_type_t;

static bool ide_initialized = false;
static semaphore ide_sem = {0};
static semaphore ide_lock = {1}; /* 1 = free */
static task_t* ide_waiter = NULL;
static uint16_t* ide_cursor = NULL;
static uint8_t ide_remaining = 0;
static ide_req_type_t ide_cur_type;

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
    }
}

static void sema_signal(semaphore* sem)
{
    atomic_store(&sem->value, 1);
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

void ide_read_sectors_monotask(uint32_t lba, uint8_t count, uint16_t* buffer)
{
    uint8_t s;
    uint32_t flags = irq_save();

    ide_wait_nonbusy();
    ide_select_drive(lba);

    outb(IDE_SECT_COUNT, count);
    outb(IDE_LBA_LOW, lba & 0xFF);
    outb(IDE_LBA_MID, (lba >> 8) & 0xFF);
    outb(IDE_LBA_HIGH, (lba >> 16) & 0xFF);
    outb(IDE_CMD, IDE_CMD_READ);

    for (s = 0; s < count; s++)
    {
        enable_interrupts();
        sema_wait(&ide_sem);
        disable_interrupts();

        if (inb(IDE_STATUS) & IDE_STATUS_ERR)
        {
            kpanic("IDE read error", 1);
        }

        insw(IDE_DATA, buffer + (uint32_t)s * 256, 256);
    }

    irq_restore(flags);
}

void ide_write_sectors_monotask(uint32_t lba, uint8_t count, uint16_t* buffer)
{
    uint8_t s;
    uint32_t flags = irq_save(); /* see ide_read_sectors_monotask() */

    ide_wait_nonbusy();
    ide_select_drive(lba);

    outb(IDE_SECT_COUNT, count);
    outb(IDE_LBA_LOW, lba & 0xFF);
    outb(IDE_LBA_MID, (lba >> 8) & 0xFF);
    outb(IDE_LBA_HIGH, (lba >> 16) & 0xFF);
    outb(IDE_CMD, IDE_CMD_WRITE);

    for (s = 0; s < count; s++)
    {
        /* The controller is ready for this sector's data as soon as the
         * previous one completed (BSY clear / DRQ set); for s==0 that's
         * immediately after the command above. */
        ide_wait_nonbusy();
        outsw(IDE_DATA, buffer + (uint32_t)s * 256, 256);

        enable_interrupts();
        sema_wait(&ide_sem);
        disable_interrupts();

        if (inb(IDE_STATUS) & IDE_STATUS_ERR)
        {
            kpanic("IDE write error", 1);
        }
    }

    irq_restore(flags);
}

void ide_read_sector_monotask(uint32_t lba, uint16_t* buffer)
{
    ide_read_sectors_monotask(lba, 1, buffer);
}

void ide_write_sector_monotask(uint32_t lba, uint16_t* buffer)
{
    ide_write_sectors_monotask(lba, 1, buffer);
}

static int ide_transfer(ide_req_type_t type, uint32_t lba, uint8_t count, uint16_t *buffer)
{
    sema_wait(&ide_lock); /* only one transfer in flight system-wide */

    disable_interrupts();

    ide_wait_nonbusy();
    ide_select_drive(lba);
    outb(IDE_SECT_COUNT, count);
    outb(IDE_LBA_LOW, lba & 0xFF);
    outb(IDE_LBA_MID, (lba >> 8) & 0xFF);
    outb(IDE_LBA_HIGH, (lba >> 16) & 0xFF);
    outb(IDE_CMD, (type == IDE_REQ_READ) ? IDE_CMD_READ : IDE_CMD_WRITE);

    ide_cursor = buffer;
    ide_remaining = count;
    ide_cur_type = type;
    ide_waiter = get_current_task();
    ide_waiter->state = TASK_WAITING;

    if (type == IDE_REQ_WRITE)
    {
        ide_wait_nonbusy();
        outsw(IDE_DATA, ide_cursor, 256);
    }

    while (ide_waiter && ide_waiter->state == TASK_WAITING)
    {
        enable_interrupts();
        asm volatile("hlt");
        disable_interrupts();
    }

    enable_interrupts();
    sema_signal(&ide_lock);
    return 0;
}

void ide_irq_handler(void)
{
    uint8_t status;
    task_t* t;

    if (!ide_waiter)
    {
        inb(IDE_STATUS);
        sema_signal(&ide_sem);
        return;
    }

    status = inb(IDE_STATUS);
    if (status & IDE_STATUS_ERR)
    {
        kpanic("IDE error in IRQ", 1);
    }

    if (ide_cur_type == IDE_REQ_READ)
        insw(IDE_DATA, ide_cursor, 256);

    ide_cursor += 256;
    ide_remaining--;

    if (ide_remaining == 0)
    {
        t = ide_waiter;
        ide_waiter = NULL;
        if (t && t->state == TASK_WAITING)
            t->state = TASK_READY;
        return;
    }

    if (ide_cur_type == IDE_REQ_WRITE)
    {
        ide_wait_nonbusy();
        outsw(IDE_DATA, ide_cursor, 256);
    }
}

int ide_read_sectors(uint32_t lba, uint8_t count, void *buffer)
{
    uint8_t* buf;

    if (lba > MAX_LBA || count == 0)
        return -1;

    buf = (uint8_t *)buffer;

    if (get_current_task() == NULL)
    {
        /* No task exists yet (early boot, e.g. ext2_mount()). */
        ide_read_sectors_monotask(lba, count, (uint16_t*)buf);
        return 0;
    }

    return ide_transfer(IDE_REQ_READ, lba, count, (uint16_t*)buf);
}

int ide_write_sectors(uint32_t lba, uint8_t count, void *buffer)
{
    uint8_t* buf;

    if (lba > MAX_LBA || count == 0)
        return -1;

    buf = (uint8_t *)buffer;

    if (get_current_task() == NULL)
    {
        ide_write_sectors_monotask(lba, count, (uint16_t*)buf);
        return 0;
    }

    return ide_transfer(IDE_REQ_WRITE, lba, count, (uint16_t*)buf);
}

void ide_start()
{
    if (ide_initialized)
        return;

    ide_initialized = true;
}
