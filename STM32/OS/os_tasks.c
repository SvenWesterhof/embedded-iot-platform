#include "os_tasks.h"
#include "os_config.h"

// Uncomment when using FreeRTOS
// #include "FreeRTOS.h"
// #include "task.h"

/**
 * @brief Initialize all RTOS tasks
 * 
 * This function creates and starts all application tasks.
 * Currently set up for bare-metal operation.
 */
void os_tasks_init(void)
{
    // Bare-metal mode - no tasks to create
    // Tasks are called sequentially from main loop
    
    // When using FreeRTOS, uncomment below:
    /*
    xTaskCreate(task_sensor_read,
                "SensorTask",
                TASK_STACK_SIZE_MEDIUM,
                NULL,
                TASK_PRIORITY_NORMAL,
                NULL);
    
    xTaskCreate(task_display_update,
                "DisplayTask",
                TASK_STACK_SIZE_MEDIUM,
                NULL,
                TASK_PRIORITY_NORMAL,
                NULL);
    
    xTaskCreate(task_communication,
                "CommTask",
                TASK_STACK_SIZE_MEDIUM,
                NULL,
                TASK_PRIORITY_LOW,
                NULL);
    */
}

// Example FreeRTOS task implementations (uncomment when using FreeRTOS)
/*
void task_sensor_read(void* pvParameters)
{
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(1000); // 1 second
    
    for (;;)
    {
        // Read sensors
        // Publish events via event bus
        
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

void task_display_update(void* pvParameters)
{
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(100); // 100 ms
    
    for (;;)
    {
        // Update display
        
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

void task_communication(void* pvParameters)
{
    for (;;)
    {
        // Handle communication
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
*/
