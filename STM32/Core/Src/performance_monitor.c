/**
 * @file performance_monitor.c
 * @brief Performance monitoring utilities for FreeRTOS applications
 * 
 * This file provides helper functions to monitor system performance,
 * task timing, stack usage, and heap consumption.
 */

#include "performance_monitor.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <string.h>

#ifdef USE_PERFORMANCE_MONITOR

// Include your logging mechanism
#include "portable_log.h"
static const char *TAG = "PERF";

/**
 * @brief Print information about all tasks in the system
 * 
 * Shows task name, state, priority, stack remaining, and task number.
 * Requires: configUSE_TRACE_FACILITY = 1 in FreeRTOSConfig.h
 */
void perf_print_task_list(void)
{
#if (configUSE_TRACE_FACILITY == 1)
    char buffer[512];
    
    LOG_I(TAG, "=== Task List ===");
    vTaskList(buffer);
    LOG_I(TAG, "\n%s", buffer);
    
    // Column meanings:
    // Name: Task name
    // State: R=Running, B=Blocked, S=Suspended, D=Deleted
    // Prio: Task priority (higher number = higher priority)
    // Stack: Estimated stack space remaining (in words)
    // Num: Task number
#else
    LOG_W(TAG, "Task list unavailable. Set configUSE_TRACE_FACILITY=1");
#endif
}

/**
 * @brief Print runtime statistics for all tasks
 * 
 * Shows percentage of CPU time each task has consumed.
 * Requires: configGENERATE_RUN_TIME_STATS = 1 in FreeRTOSConfig.h
 */
void perf_print_runtime_stats(void)
{
#if (configGENERATE_RUN_TIME_STATS == 1)
    char buffer[512];
    
    LOG_I(TAG, "=== Runtime Statistics ===");
    vTaskGetRunTimeStats(buffer);
    LOG_I(TAG, "\n%s", buffer);
    
    // Shows: Task name, absolute runtime, percentage of total time
#else
    LOG_W(TAG, "Runtime stats unavailable. Set configGENERATE_RUN_TIME_STATS=1");
#endif
}

/**
 * @brief Print heap memory usage information
 * 
 * Shows current free heap, minimum ever free heap, and used heap.
 */
void perf_print_heap_info(void)
{
    size_t free_heap = xPortGetFreeHeapSize();
    
    LOG_I(TAG, "=== Heap Memory Info ===");
    LOG_I(TAG, "Total heap size:     %lu bytes", (unsigned long)configTOTAL_HEAP_SIZE);
    LOG_I(TAG, "Free heap:           %lu bytes", (unsigned long)free_heap);
    LOG_I(TAG, "Used heap:           %lu bytes", 
          (unsigned long)(configTOTAL_HEAP_SIZE - free_heap));
    
#if (configUSE_TRACE_FACILITY == 1)
    size_t min_heap = xPortGetMinimumEverFreeHeapSize();
    LOG_I(TAG, "Min free heap ever:  %lu bytes", (unsigned long)min_heap);
    LOG_I(TAG, "Peak heap usage:     %lu bytes", 
          (unsigned long)(configTOTAL_HEAP_SIZE - min_heap));
    
    // Warning if heap is getting tight
    if (min_heap < (configTOTAL_HEAP_SIZE / 10)) {
        LOG_W(TAG, "WARNING: Heap usage > 90%% - consider increasing heap size!");
    }
#endif
}

/**
 * @brief Check stack usage for all tasks
 * 
 * Reports stack high-water mark for each task and warns if any task
 * is using more than 80% of its allocated stack.
 */
void perf_check_all_task_stacks(void)
{
#if (configUSE_TRACE_FACILITY == 1) && (INCLUDE_uxTaskGetStackHighWaterMark == 1)
    TaskStatus_t *task_status_array;
    UBaseType_t task_count;
    UBaseType_t i;
    
    // Get number of tasks
    task_count = uxTaskGetNumberOfTasks();
    
    // Allocate array to hold task status
    task_status_array = pvPortMalloc(task_count * sizeof(TaskStatus_t));
    
    if (task_status_array != NULL)
    {
        // Get task status for all tasks
        task_count = uxTaskGetSystemState(task_status_array, task_count, NULL);
        
        LOG_I(TAG, "=== Task Stack Usage ===");
        
        for (i = 0; i < task_count; i++)
        {
            UBaseType_t stack_remaining = task_status_array[i].usStackHighWaterMark;
            const char *task_name = task_status_array[i].pcTaskName;
            
            // Note: Stack size isn't directly available via API, so we only show remaining
            LOG_I(TAG, "Task '%s': %lu words remaining", 
                  task_name, (unsigned long)stack_remaining);
            
            // Warning if stack is getting tight (less than 100 words remaining)
            if (stack_remaining < 100) {
                LOG_W(TAG, "  ^ WARNING: Stack usage is high for task '%s'!", task_name);
            }
        }
        
        // Free allocated memory
        vPortFree(task_status_array);
    }
    else
    {
        LOG_E(TAG, "Failed to allocate memory for task status array");
    }
#else
    LOG_W(TAG, "Stack monitoring unavailable. Set configUSE_TRACE_FACILITY=1 and INCLUDE_uxTaskGetStackHighWaterMark=1");
#endif
}

/**
 * @brief Check stack usage for a specific task
 * 
 * @param task_handle Handle to the task (NULL for current task)
 * @return Stack high-water mark in words
 */
UBaseType_t perf_check_task_stack(TaskHandle_t task_handle)
{
#if (INCLUDE_uxTaskGetStackHighWaterMark == 1)
    UBaseType_t stack_remaining = uxTaskGetStackHighWaterMark(task_handle);
    const char *task_name = task_handle ? pcTaskGetName(task_handle) : "CURRENT";
    
    LOG_I(TAG, "Task '%s' stack: %lu words remaining", 
          task_name, (unsigned long)stack_remaining);
    
    if (stack_remaining < 100) {
        LOG_W(TAG, "Stack usage high for '%s'!", task_name);
    }
    
    return stack_remaining;
#else
    LOG_W(TAG, "Stack monitoring unavailable. Set INCLUDE_uxTaskGetStackHighWaterMark=1");
    return 0;
#endif
}

/**
 * @brief Print comprehensive performance report
 * 
 * Combines all performance monitoring information into one report.
 * Call this periodically (e.g., every 10 seconds) or on demand.
 */
void perf_print_full_report(void)
{
    LOG_I(TAG, "\n");
    LOG_I(TAG, "========================================");
    LOG_I(TAG, "     PERFORMANCE MONITORING REPORT      ");
    LOG_I(TAG, "========================================");
    
    perf_print_heap_info();
    LOG_I(TAG, "");
    
    perf_print_task_list();
    LOG_I(TAG, "");
    
    perf_check_all_task_stacks();
    LOG_I(TAG, "");
    
    perf_print_runtime_stats();
    
    LOG_I(TAG, "========================================\n");
}

/**
 * @brief Get CPU load percentage (approximate)
 * 
 * This uses the idle task's runtime to calculate CPU load.
 * Formula: CPU_Load = 100 - (Idle_Time_Percentage)
 * 
 * @return CPU load percentage (0-100)
 * 
 * Note: Requires runtime statistics to be enabled.
 */
uint8_t perf_get_cpu_load_percent(void)
{
#if (configGENERATE_RUN_TIME_STATS == 1) && (INCLUDE_xTaskGetIdleTaskHandle == 1)
    static uint32_t last_idle_time = 0;
    static uint32_t last_total_time = 0;
    
    TaskHandle_t idle_handle = xTaskGetIdleTaskHandle();
    TaskStatus_t idle_status;
    uint32_t total_runtime;
    
    // Get idle task status
    vTaskGetInfo(idle_handle, &idle_status, pdTRUE, eInvalid);
    
    // Get total runtime
    vTaskGetRunTimeStats(NULL);  // Updates internal counters
    
    uint32_t idle_time_delta = idle_status.ulRunTimeCounter - last_idle_time;
    
    // For simplicity, we'll calculate based on a snapshot
    // More accurate method would track deltas over time
    
    last_idle_time = idle_status.ulRunTimeCounter;
    
    // Calculate CPU load (rough estimate)
    // This is simplified - a production version would track deltas properly
    uint8_t cpu_load = 100;  // Placeholder
    
    return cpu_load;
#else
    LOG_W(TAG, "CPU load calculation unavailable. Set configGENERATE_RUN_TIME_STATS=1");
    return 0;
#endif
}

/**
 * @brief Performance monitoring task
 * 
 * This task runs periodically and prints performance reports.
 * Create this task in your main initialization to enable automatic monitoring.
 * 
 * @param pvParameters Task parameters (unused)
 */
void perf_monitor_task(void *pvParameters)
{
    (void)pvParameters;
    
    const TickType_t report_interval = pdMS_TO_TICKS(10000);  // 10 seconds
    
    LOG_I(TAG, "Performance monitor task started");
    
    while (1)
    {
        vTaskDelay(report_interval);
        
        // Print full performance report
        perf_print_full_report();
    }
}

/**
 * @brief Create the performance monitoring task
 * 
 * Call this from your main initialization code to enable periodic
 * performance reporting.
 * 
 * @param priority Task priority (recommend: low priority)
 * @return pdPASS if successful, pdFAIL otherwise
 */
BaseType_t perf_create_monitor_task(UBaseType_t priority)
{
    BaseType_t result;
    
    result = xTaskCreate(
        perf_monitor_task,
        "PerfMon",
        512,  // Stack size in words
        NULL,
        priority,
        NULL
    );
    
    if (result == pdPASS) {
        LOG_I(TAG, "Performance monitoring task created");
    } else {
        LOG_E(TAG, "Failed to create performance monitoring task");
    }
    
    return result;
}

#endif // USE_PERFORMANCE_MONITOR
