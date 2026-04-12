/* getting offsets for tasks.asm schedule() */
#include <stdio.h>
#include <stddef.h>
#include "task.h"

int main(void)
{
    printf("%%define TASK_ESP_OFFSET          %zu\n", offsetof(task_t, cpu_esp_));
    printf("%%define TASK_KERNEL_STACK_OFFSET %zu\n", offsetof(task_t, kernel_stack));
    printf("%%define TASK_ENV_OFFSET          %zu\n", offsetof(task_t, env));
    printf("%%define TASK_PAGE_DIR_OFFSET     %zu\n", offsetof(task_t, page_dir));
    return 0;
}
