#ifndef OS_TASKS_H
#define OS_TASKS_H

/**
 * @file os_tasks.h
 * @brief RTOS Task Definitions (FreeRTOS)
 * 
 * This module defines all RTOS tasks for the application.
 * Currently set up for bare-metal (no RTOS), but can be extended
 * to use FreeRTOS or other RTOS.
 */

#include <stdint.h>

// Task Priorities (if using FreeRTOS)
#define TASK_PRIORITY_HIGH      3
#define TASK_PRIORITY_NORMAL    2
#define TASK_PRIORITY_LOW       1

// Task Stack Sizes (in words, for FreeRTOS)
#define TASK_STACK_SIZE_SMALL   128
#define TASK_STACK_SIZE_MEDIUM  256
#define TASK_STACK_SIZE_LARGE   512

// Task Functions (implement these in os_tasks.c)
void os_tasks_init(void);

// Example task functions (uncomment when using FreeRTOS)
// void task_sensor_read(void* pvParameters);
// void task_display_update(void* pvParameters);
// void task_communication(void* pvParameters);

#endif // OS_TASKS_H
