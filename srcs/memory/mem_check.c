#include "kmalloc.h"
#include "../tasks/task.h"
#include "../time/time.h"
#include "../display/display.h"

#define MEM_CHECK_INTERVAL_SECONDS 15

void mem_check_task_main(void)
{
    uint32_t heap_problems;
    uint32_t dead_owner_allocs;

    while (1)
    {
        _sleep(MEM_CHECK_INTERVAL_SECONDS);

        heap_problems = kmalloc_audit_quiet();
        dead_owner_allocs = kmalloc_check_dead_owners();

        if (heap_problems || dead_owner_allocs)
        {
            kprintf("[mem_check] %d heap corruption issue(s), %d allocation(s) from dead task(s) -- see above\n",
                    heap_problems, dead_owner_allocs);
        }
    }
}
