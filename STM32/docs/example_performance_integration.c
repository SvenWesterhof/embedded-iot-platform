/**
 * @file example_performance_integration.c
 * @brief Example showing how to integrate performance monitoring into your project
 * 
 * This file shows different ways to add performance monitoring to your
 * existing STM32 FreeRTOS project.
 */

// EXAMPLE 1: Automatic Monitoring Task
// =====================================
// Add this to your main.c or freertos.c

#include "FreeRTOS.h"
#include "task.h"
#include "performance_monitor.h"

void example_automatic_monitoring(void)
{
    // After creating all your application tasks, add monitoring task
    // This will automatically print reports every 10 seconds
    
    perf_create_monitor_task(tskIDLE_PRIORITY + 1);
}


// EXAMPLE 2: Manual Reporting
// ============================
// Call from any task when you want a report

void example_manual_report_from_task(void *pvParameters)
{
    while(1)
    {
        // Your normal task work here
        // ...
        
        // Print performance report every 30 seconds
        vTaskDelay(pdMS_TO_TICKS(30000));
        perf_print_full_report();
    }
}


// EXAMPLE 3: Selective Monitoring
// ================================
// Only check specific metrics

void example_selective_monitoring(void *pvParameters)
{
    while(1)
    {
        // Your task work
        // ...
        
        vTaskDelay(pdMS_TO_TICKS(5000));
        
        // Just check heap
        perf_print_heap_info();
        
        // Or just check this task's stack
        perf_check_task_stack(NULL);  // NULL = current task
    }
}


// EXAMPLE 4: Periodic Lightweight Checks
// =======================================
// Check critical metrics without full report

void example_lightweight_monitoring(void *pvParameters)
{
    while(1)
    {
        // Your task work
        // ...
        
        // Quick check every loop iteration
        UBaseType_t stack_left = uxTaskGetStackHighWaterMark(NULL);
        if (stack_left < 100) {
            // Stack getting low - log warning
        }
        
        size_t heap_free = xPortGetFreeHeapSize();
        if (heap_free < 1024) {
            // Heap getting low - log warning
        }
        
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}


// EXAMPLE 5: On-Demand Reporting via Command
// ===========================================
// Trigger report from UART command or button

void example_command_triggered_report(char *command)
{
    if (strcmp(command, "perf") == 0) {
        perf_print_full_report();
    }
    else if (strcmp(command, "tasks") == 0) {
        perf_print_task_list();
    }
    else if (strcmp(command, "heap") == 0) {
        perf_print_heap_info();
    }
    else if (strcmp(command, "stack") == 0) {
        perf_check_all_task_stacks();
    }
}


// EXAMPLE 6: Integration with Existing Task
// ==========================================
// Add monitoring to your existing sensor task

#include "portable_log.h"

void sensor_task(void *pvParameters)
{
    static const char *TAG = "SENSOR";
    TickType_t last_wake = xTaskGetTickCount();
    
    // Add periodic performance check
    uint32_t perf_counter = 0;
    const uint32_t PERF_CHECK_INTERVAL = 60;  // Every 60 iterations
    
    while(1)
    {
        // Your existing sensor reading code
        // read_sensors();
        // process_data();
        // ...
        
        // Periodic performance check
        if (++perf_counter >= PERF_CHECK_INTERVAL) {
            perf_counter = 0;
            
            // Check stack usage
            UBaseType_t stack = uxTaskGetStackHighWaterMark(NULL);
            LOG_D(TAG, "Stack remaining: %lu words", stack);
            
            if (stack < 100) {
                LOG_W(TAG, "Stack usage HIGH!");
            }
        }
        
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(100));
    }
}


// EXAMPLE 7: Complete Integration Example
// ========================================
// Shows how to integrate into a full application

#include "main.h"
#include "FreeRTOS.h"
#include "task.h"
#include "performance_monitor.h"
#include "portable_log.h"

// Task handles
TaskHandle_t sensor_task_handle;
TaskHandle_t display_task_handle;
TaskHandle_t comm_task_handle;

// Task prototypes
void sensor_task(void *pvParameters);
void display_task(void *pvParameters);
void communication_task(void *pvParameters);

int main(void)
{
    // Hardware initialization
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_USART3_UART_Init();
    // ... other peripheral init
    
    // Create application tasks
    xTaskCreate(sensor_task, "Sensor", 
                512, NULL, 3, &sensor_task_handle);
    
    xTaskCreate(display_task, "Display", 
                512, NULL, 2, &display_task_handle);
    
    xTaskCreate(communication_task, "Comm", 
                512, NULL, 1, &comm_task_handle);
    
    // Add performance monitoring task (optional)
    #ifdef USE_PERFORMANCE_MONITOR
    perf_create_monitor_task(tskIDLE_PRIORITY + 1);
    LOG_I("MAIN", "Performance monitoring enabled");
    #endif
    
    // Start scheduler
    vTaskStartScheduler();
    
    // Should never reach here
    while(1);
}

void sensor_task(void *pvParameters)
{
    TickType_t last_wake = xTaskGetTickCount();
    
    while(1)
    {
        // Read sensors
        // ...
        
        // Optional: Check stack periodically
        #ifdef DEBUG
        static uint32_t counter = 0;
        if (++counter % 100 == 0) {
            perf_check_task_stack(NULL);
        }
        #endif
        
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(100));
    }
}

void display_task(void *pvParameters)
{
    while(1)
    {
        // Update display
        // ...
        
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void communication_task(void *pvParameters)
{
    while(1)
    {
        // Handle communication
        // ...
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}


// EXAMPLE 8: Error Handlers with Performance Info
// ================================================
// Enhanced error handlers that dump performance info

void enhanced_stack_overflow_hook(TaskHandle_t xTask, signed char *pcTaskName)
{
    // Disable interrupts
    __disable_irq();
    
    // Log error
    LOG_E("ERROR", "Stack overflow in task: %s", pcTaskName);
    
    // Dump all task stacks
    perf_check_all_task_stacks();
    
    // Halt for debugger
    while(1);
}

void enhanced_malloc_failed_hook(void)
{
    __disable_irq();
    
    LOG_E("ERROR", "Malloc failed!");
    
    // Show heap status
    perf_print_heap_info();
    
    // Show all tasks (which might be allocating)
    perf_print_task_list();
    
    while(1);
}


// EXAMPLE 9: Conditional Compilation
// ===================================
// Control monitoring with build flags

void conditional_monitoring_example(void)
{
    // Development build - full monitoring
    #ifdef DEBUG
        perf_create_monitor_task(tskIDLE_PRIORITY + 1);
        LOG_I("MAIN", "Debug mode: Performance monitoring active");
    #else
        // Production build - no automatic monitoring, but functions available on demand
        LOG_I("MAIN", "Release mode: Performance monitoring available on command");
    #endif
}


// EXAMPLE 10: Integration with CLI/Shell
// =======================================
// Add performance commands to your command-line interface

typedef struct {
    const char *command;
    void (*handler)(void);
    const char *help;
} cli_command_t;

void cli_cmd_perf(void) { perf_print_full_report(); }
void cli_cmd_tasks(void) { perf_print_task_list(); }
void cli_cmd_heap(void) { perf_print_heap_info(); }
void cli_cmd_stacks(void) { perf_check_all_task_stacks(); }

const cli_command_t perf_commands[] = {
    {"perf",   cli_cmd_perf,   "Print full performance report"},
    {"tasks",  cli_cmd_tasks,  "List all tasks"},
    {"heap",   cli_cmd_heap,   "Show heap usage"},
    {"stacks", cli_cmd_stacks, "Check all task stacks"},
    {NULL, NULL, NULL}
};

// Register these commands with your CLI system


// SUMMARY: Choose What Works for You
// ===================================
/*
 * Option 1: Automatic (easiest)
 *   - Add perf_create_monitor_task() in main()
 *   - Get reports every 10 seconds automatically
 * 
 * Option 2: Manual on-demand
 *   - Call perf_print_full_report() when you want
 *   - From task, button press, UART command, etc.
 * 
 * Option 3: Lightweight
 *   - Just check specific metrics in your tasks
 *   - Minimal overhead
 * 
 * Option 4: CLI integrated
 *   - Add commands to your command-line interface
 *   - Query performance on demand
 * 
 * Mix and match based on your needs!
 */
