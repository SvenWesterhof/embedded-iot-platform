#ifndef OS_TASKS_H
#define OS_TASKS_H

#include <stdbool.h>

/**
 * @brief Initialize OS tasks
 * @return true if successful
 */
bool os_tasks_init(void);

/**
 * @brief Start all OS tasks
 */
void os_tasks_start(void);

#endif // OS_TASKS_H
