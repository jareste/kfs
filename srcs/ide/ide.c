
#include "ide.h"
#include "../io/io.h"
#include "../utils/utils.h"
#include "../memory/memory.h"
#include "../display/display.h"
#include "../tasks/task.h"
#include "../panic/kpanic.h"

#define MAX_LBA        0x0FFFFFFF
#define IDE_QUEUE_SIZE 64

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

typedef struct ide_request
{
    ide_req_type_t   type;
    uint32_t         lba;
    void            *buffer;
    pid_t            owner_pid;
    struct ide_request *next;
} ide_request_t;

typedef struct
{
    ide_request_t *head;
    ide_request_t *tail;
    volatile int   busy;
} ide_queue_t;

static ide_queue_t   ide_queue    = {0};
static pid_t         ide_task_pid = (pid_t)0;

static volatile ide_request_t *current_req = NULL;

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

void ide_read_sector_monotask(uint32_t lba, uint16_t* buffer)
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
        kpanic("IDE read error", 1);
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

void ide_write_sector_monotask(uint32_t lba, uint16_t* buffer)
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
        kpanic("IDE write error", 1);
    }
    
    enable_interrupts();
}

static void queue_push(ide_request_t *req)
{
    req->next = NULL;
    if (!ide_queue.tail) {
        ide_queue.head = ide_queue.tail = req;
    } else {
        ide_queue.tail->next = req;
        ide_queue.tail = req;
    }
}

static ide_request_t *queue_pop(void)
{
    if (!ide_queue.head)
        return NULL;
    ide_request_t *r = ide_queue.head;
    ide_queue.head = r->next;
    if (!ide_queue.head)
        ide_queue.tail = NULL;
    return r;
}

static void ide_dispatch(ide_request_t *req)
{
    ide_queue.busy = 1;
    current_req    = req;

    /* Wait controller to be free */
    while (inb(IDE_STATUS) & IDE_STATUS_BSY);

    outb(IDE_DRIVE_SEL, 0xE0 | ((req->lba >> 24) & 0x0F));

    outb(IDE_SECT_COUNT, 1);
    outb(IDE_LBA_LOW,  (req->lba)       & 0xFF);
    outb(IDE_LBA_MID,  (req->lba >> 8)  & 0xFF);
    outb(IDE_LBA_HIGH, (req->lba >> 16) & 0xFF);

    if (req->type == IDE_REQ_READ)
    {
        outb(IDE_CMD, IDE_CMD_READ);
    }
    else
    {
        outb(IDE_CMD, IDE_CMD_WRITE);
        uint16_t *src = (uint16_t *)req->buffer;
        for (int i = 0; i < 256; i++)
            outw(IDE_DATA, src[i]);
    }
}

void ide_irq_handler(void)
{
    if (ide_task_pid == 0) /* Monotask */
    {
        inb(IDE_STATUS);
        sema_signal(&ide_sem);
        return;
    }

    if (!current_req)
    {
        return;
    }

    if (inb(IDE_STATUS) & IDE_STATUS_ERR)
    {
        kpanic("IDE error en IRQ", 1);
    }

    ide_request_t *req = (ide_request_t *)current_req;

    if (req->type == IDE_REQ_READ)
    {
        uint16_t *dst = (uint16_t *)req->buffer;
        for (int i = 0; i < 256; i++)
            dst[i] = inw(IDE_DATA);
    }

    task_t *owner = get_task_by_pid(req->owner_pid);
    if (owner && owner->state == TASK_WAITING)
        owner->state = TASK_READY;

    kfree(req);
    current_req    = NULL;
    ide_queue.busy = 0;

    task_t *ide_task = get_task_by_pid(ide_task_pid);
    if (ide_task && ide_task->state == TASK_WAITING)
        ide_task->state = TASK_READY;
}

/* Single task that handles IDE requests */
void ide_task_main(void)
{
    ide_task_pid = _getpid();

    while (1)
    {
        disable_interrupts();

        if (!ide_queue.busy && ide_queue.head)
        {
            ide_request_t *req = queue_pop();
            /* Dispatch with disabled interrupts
             * ide_dispatch() would finish before IRQ, as the controller needs time to process the command.
             * We enable interrupts right after to allow IRQ to arrive. 
             */
            ide_dispatch(req);
            enable_interrupts();
        }
        else
        {
            /* Make task sleep. ide_enqueue() would wake it up. */
            get_current_task()->state = TASK_WAITING;
            enable_interrupts();
            /* on next IRQ scheduler would run */
            asm volatile("hlt");
        }
    }
}

static int ide_submit(ide_req_type_t type, uint32_t lba, void *buffer)
{
    ide_request_t* req = kmalloc(sizeof(ide_request_t));
    if (!req) return -1;

    req->type      = type;
    req->lba       = lba;
    req->buffer    = buffer;
    req->owner_pid = _getpid();
    req->next      = NULL;

    disable_interrupts();

    queue_push(req);

    task_t *ide_task = get_task_by_pid(ide_task_pid);
    if (ide_task && ide_task->state == TASK_WAITING)
        ide_task->state = TASK_READY;

    /* Block current task until the request is completed */
    get_current_task()->state = TASK_WAITING;

    enable_interrupts();
    asm volatile("hlt");

    /* buffer already has the data */
    return 0;
}

int ide_enqueue_read(uint32_t lba, void *buffer)
{
    return ide_submit(IDE_REQ_READ, lba, buffer);
}

int ide_enqueue_write(uint32_t lba, const void *buffer)
{
    return ide_submit(IDE_REQ_WRITE, lba, (void *)buffer);
}

int ide_read_sectors(uint32_t lba, uint8_t count, void *buffer)
{
    if (lba > MAX_LBA || count == 0) return -1;
    uint8_t *buf = (uint8_t *)buffer;
    for (uint8_t i = 0; i < count; i++)
    {
#warning think how to handle properly the monotask case, maybe move it to a separate function?
        if (ide_task_pid == 0) /* monotask, maybe rethink it? */
            ide_read_sector_monotask(lba + i, (uint16_t*)(buf + (i * IDE_SECTOR_SIZE)));
        else if (ide_enqueue_read(lba + i, buf + (i * IDE_SECTOR_SIZE)) < 0)
            return -1;
    }
    return 0;
}

int ide_write_sectors(uint32_t lba, uint8_t count, void *buffer)
{
    if (lba > MAX_LBA || count == 0) return -1;
    uint8_t *buf = (uint8_t *)buffer;
    for (uint8_t i = 0; i < count; i++)
    {
#warning think how to handle properly the monotask case, maybe move it to a separate function?
        if (ide_task_pid == 0)
            ide_write_sector_monotask(lba + i, (uint16_t*)(buf + (i * IDE_SECTOR_SIZE)));
        else if (ide_enqueue_write(lba + i, buf + (i * IDE_SECTOR_SIZE)) < 0)
            return -1;
    }
    return 0;
}
