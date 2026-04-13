#include "task_offsets.h"

/* ensure proper offsets for tasks.asm schedule() */
_Static_assert(TASK_ESP_OFFSET_VAL          == 36, "offset sanity");
_Static_assert(TASK_KERNEL_STACK_OFFSET_VAL == 44, "offset sanity");
_Static_assert(TASK_ENV_OFFSET_VAL          == 60, "offset sanity");
_Static_assert(TASK_PAGE_DIR_OFFSET_VAL     == 56, "offset sanity");
