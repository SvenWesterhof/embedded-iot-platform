/**
 * @file performance_monitor.h
 * @brief Performance monitoring utilities for FreeRTOS applications
 * 
 * Provides functions to monitor:
 * - Task execution and timing
 * - Stack usage per task
 * - Heap memory consumption
 * - CPU load
 * 
 * Usage:
 * 1. Enable USE_PERFORMANCE_MONITOR in your build
 * 2. Call perf_print_full_report() periodically or on demand
 * 3. Or create automatic monitoring task with perf_create_monitor_task()
 */

#ifndef PERFORMANCE_MONITOR_H
#define PERFORMANCE_MONITOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "FreeRTOS.h"
#include "task.h"

// Enable/disable performance monitoring
// Define this in your build system or here
#ifndef USE_PERFORMANCE_MONITOR
#define USE_PERFORMANCE_MONITOR 1
#endif

#ifdef USE_PERFORMANCE_MONITOR

/**
 * @brief Print information about all tasks in the system
 * 
 * Shows task name, state, priority, and stack remaining.
 * Requires: configUSE_TRACE_FACILITY = 1
 */
void perf_print_task_list(void);

/**
 * @brief Print runtime statistics for all tasks
 * 
 * Shows percentage of CPU time each task has consumed.
 * Requires: configGENERATE_RUN_TIME_STATS = 1
 */
void perf_print_runtime_stats(void);

/**
 * @brief Print heap memory usage information
 * 
 * Shows current free heap, minimum ever free heap, and used heap.
 */
void perf_print_heap_info(void);

/**
 * @brief Check stack usage for all tasks
 * 
 * Reports stack high-water mark for each task.
 * Requires: configUSE_TRACE_FACILITY = 1 and 
 *           INCLUDE_uxTaskGetStackHighWaterMark = 1
 */
void perf_check_all_task_stacks(void);

/**
 * @brief Check stack usage for a specific task
 * 
 * @param task_handle Handle to the task (NULL for current task)
 * @return Stack high-water mark in words
 * 
 * Requires: INCLUDE_uxTaskGetStackHighWaterMark = 1
 */
UBaseType_t perf_check_task_stack(TaskHandle_t task_handle);

/**
 * @brief Print comprehensive performance report
 * 
 * Combines all performance monitoring information.
 * Recommended to call every 10-30 seconds during development.
 */
void perf_print_full_report(void);

/**
 * @brief Get approximate CPU load percentage
 * 
 * @return CPU load percentage (0-100)
 * 
 * Requires: configGENERATE_RUN_TIME_STATS = 1 and
 *           INCLUDE_xTaskGetIdleTaskHandle = 1
 */
uint8_t perf_get_cpu_load_percent(void);

/**
 * @brief Performance monitoring task
 * 
 * This task runs periodically and prints performance reports.
 * Don't call directly - use perf_create_monitor_task() instead.
 * 
 * @param pvParameters Task parameters (unused)
 */
void perf_monitor_task(void *pvParameters);

/**
 * @brief Create the performance monitoring task
 * 
 * Creates a low-priority task that periodically prints performance reports.
 * 
 * Example usage in main():
 * @code
 * // After creating your application tasks
 * perf_create_monitor_task(tskIDLE_PRIORITY + 1);
 * @endcode
 * 
 * @param priority Task priority (recommend: tskIDLE_PRIORITY + 1)
 * @return pdPASS if successful, pdFAIL otherwise
 */
BaseType_t perf_create_monitor_task(UBaseType_t priority);

// Inline helper macros for quick stack checks in tasks
#if (INCLUDE_uxTaskGetStackHighWaterMark == 1)
#define PERF_CHECK_CURRENT_STACK() \
    do { \
        UBaseType_t stack = uxTaskGetStackHighWaterMark(NULL); \
        if (stack < 100) { \
            /* Log warning */ \
        } \
    } while(0)
#else
#define PERF_CHECK_CURRENT_STACK() do {} while(0)
#endif

#else // !USE_PERFORMANCE_MONITOR

// Stub functions when performance monitoring is disabled
#define perf_print_task_list()          do {} while(0)
#define perf_print_runtime_stats()      do {} while(0)
#define perf_print_heap_info()          do {} while(0)
#define perf_check_all_task_stacks()    do {} while(0)
#define perf_check_task_stack(h)        (0)
#define perf_print_full_report()        do {} while(0)
#define perf_get_cpu_load_percent()     (0)
#define perf_create_monitor_task(p)     (pdFAIL)
#define PERF_CHECK_CURRENT_STACK()      do {} while(0)

#endif // USE_PERFORMANCE_MONITOR

#ifdef __cplusplus
}
#endif

#endif // PERFORMANCE_MONITOR_H
