#ifndef TASK_OFFSETS_H
#define TASK_OFFSETS_H

#include <stddef.h>
#include "task.h"

#define TASK_ESP_OFFSET_VAL          offsetof(task_t, cpu_esp_)
#define TASK_KERNEL_STACK_OFFSET_VAL offsetof(task_t, kernel_stack)
#define TASK_ENV_OFFSET_VAL          offsetof(task_t, env)
#define TASK_PAGE_DIR_OFFSET_VAL     offsetof(task_t, page_dir)

#endif
