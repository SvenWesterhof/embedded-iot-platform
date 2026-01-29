#include "os_wrapper.h"
#include "../Drivers_BSP/Custom/portable_log.h"

// Platform-specific FreeRTOS includes
#if defined(ESP_PLATFORM) || defined(IDF_VER)
    // ESP32/ESP-IDF
    #include <freertos/FreeRTOS.h>
    #include <freertos/task.h>
    #include <freertos/queue.h>
    #include <freertos/semphr.h>
#else
    // STM32 or other platforms
    #include "FreeRTOS.h"
    #include "task.h"
    #include "queue.h"
    #include "semphr.h"
#endif

static const char *TAG = "OS_WRAPPER";

// Platform detection for core affinity
#if defined(ESP_PLATFORM)
    // ESP32 platform detection
    #if defined(CONFIG_FREERTOS_UNICORE) || \
        (!defined(CONFIG_IDF_TARGET_ESP32) && !defined(CONFIG_IDF_TARGET_ESP32S3))
        #define OS_SINGLE_CORE 1
    #else
        #define OS_SINGLE_CORE 0
    #endif
    
    // ESP32 dual-core variants support core pinning
    #if !defined(OS_SINGLE_CORE) || (OS_SINGLE_CORE == 0)
        #define OS_HAS_CORE_AFFINITY 1
    #else
        #define OS_HAS_CORE_AFFINITY 0
    #endif
#elif defined(STM32H747xx) || defined(STM32H757xx) || defined(STM32H745xx) || defined(STM32H755xx)
    // STM32H7 dual-core variants (Cortex-M7 + Cortex-M4)
    #define OS_SINGLE_CORE 0
    #define OS_HAS_CORE_AFFINITY 0  // FreeRTOS AMP not directly supported via standard API
#else
    // All other platforms (most STM32s are single-core)
    #define OS_SINGLE_CORE 1
    #define OS_HAS_CORE_AFFINITY 0
#endif

// =============================================================================
// INITIALIZATION
// =============================================================================

os_result_t os_init(void)
{
    //LOG_I(TAG, "OS wrapper initialized (FreeRTOS backend)");
    return OS_SUCCESS;
}

// =============================================================================
// QUEUE OPERATIONS
// =============================================================================

os_queue_handle_t os_queue_create(uint32_t queue_length, uint32_t item_size)
{
    if (queue_length == 0 || item_size == 0) {
        LOG_E(TAG, "Invalid queue parameters");
        return NULL;
    }

    QueueHandle_t queue = xQueueCreate(queue_length, item_size);
    if (queue == NULL) {
        LOG_E(TAG, "Failed to create queue");
        return NULL;
    }

    LOG_D(TAG, "Created queue: length=%lu, item_size=%lu", queue_length, item_size);
    return (os_queue_handle_t)queue;
}

void os_queue_delete(os_queue_handle_t queue)
{
    if (queue == NULL) {
        LOG_W(TAG, "Attempt to delete NULL queue");
        return;
    }

    vQueueDelete((QueueHandle_t)queue);
    LOG_D(TAG, "Queue deleted");
}

os_result_t os_queue_send(os_queue_handle_t queue, const void* item, uint32_t timeout_ms)
{
    if (queue == NULL || item == NULL) {
        return OS_INVALID_PARAM;
    }

    TickType_t timeout_ticks;
    if (timeout_ms == OS_WAIT_FOREVER) {
        timeout_ticks = portMAX_DELAY;
    } else if (timeout_ms == OS_NO_WAIT) {
        timeout_ticks = 0;
    } else {
        timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    }

    BaseType_t result = xQueueSend((QueueHandle_t)queue, item, timeout_ticks);
    
    switch (result) {
        case pdPASS:
            return OS_SUCCESS;
        case errQUEUE_FULL:
            return OS_FULL;
        default:
            return OS_ERROR;
    }
}

os_result_t os_queue_send_from_isr(os_queue_handle_t queue, const void* item, bool* higher_priority_task_woken)
{
    if (queue == NULL || item == NULL) {
        return OS_INVALID_PARAM;
    }

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    BaseType_t result = xQueueSendFromISR((QueueHandle_t)queue, item, &xHigherPriorityTaskWoken);
    
    if (higher_priority_task_woken != NULL) {
        *higher_priority_task_woken = (xHigherPriorityTaskWoken == pdTRUE);
    }
    
    return (result == pdPASS) ? OS_SUCCESS : OS_FULL;
}

os_result_t os_queue_receive(os_queue_handle_t queue, void* item, uint32_t timeout_ms)
{
    if (queue == NULL || item == NULL) {
        return OS_INVALID_PARAM;
    }

    TickType_t timeout_ticks;
    if (timeout_ms == OS_WAIT_FOREVER) {
        timeout_ticks = portMAX_DELAY;
    } else if (timeout_ms == OS_NO_WAIT) {
        timeout_ticks = 0;
    } else {
        timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    }

    BaseType_t result = xQueueReceive((QueueHandle_t)queue, item, timeout_ticks);
    
    switch (result) {
        case pdPASS:
            return OS_SUCCESS;
        case errQUEUE_EMPTY:
            return OS_EMPTY;
        default:
            return OS_TIMEOUT;
    }
}

os_result_t os_queue_receive_from_isr(os_queue_handle_t queue, void* item, bool* higher_priority_task_woken)
{
    if (queue == NULL || item == NULL) {
        return OS_INVALID_PARAM;
    }

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    BaseType_t result = xQueueReceiveFromISR((QueueHandle_t)queue, item, &xHigherPriorityTaskWoken);
    
    if (higher_priority_task_woken != NULL) {
        *higher_priority_task_woken = (xHigherPriorityTaskWoken == pdTRUE);
    }
    
    return (result == pdPASS) ? OS_SUCCESS : OS_EMPTY;
}

uint32_t os_queue_get_count(os_queue_handle_t queue)
{
    if (queue == NULL) {
        return 0;
    }

    return (uint32_t)uxQueueMessagesWaiting((QueueHandle_t)queue);
}

os_result_t os_queue_reset(os_queue_handle_t queue)
{
    if (queue == NULL) {
        return OS_INVALID_PARAM;
    }

    xQueueReset((QueueHandle_t)queue);
    return OS_SUCCESS;
}

// =============================================================================
// TASK OPERATIONS
// =============================================================================

os_result_t os_task_create(os_task_func_t task_func, const char* name, 
                          uint32_t stack_size, void* params, 
                          uint8_t priority, os_task_handle_t* handle)
{
    if (task_func == NULL) {
        return OS_INVALID_PARAM;
    }

    TaskHandle_t task_handle = NULL;
    BaseType_t result = xTaskCreate(
        (TaskFunction_t)task_func,
        name ? name : "unnamed_task",
        stack_size / sizeof(StackType_t),  // FreeRTOS expects stack size in words
        params,
        priority,
        &task_handle
    );

    if (result != pdPASS) {
        LOG_E(TAG, "Failed to create task: %s", name ? name : "unnamed");
        return (result == errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY) ? OS_NO_MEMORY : OS_ERROR;
    }

    if (handle != NULL) {
        *handle = (os_task_handle_t)task_handle;
    }

    LOG_D(TAG, "Created task: %s, priority=%d, stack=%lu", 
          name ? name : "unnamed", priority, stack_size);
    return OS_SUCCESS;
}

os_result_t os_task_create_pinned(os_task_func_t task_func, const char* name, 
                                 uint32_t stack_size, void* params, 
                                 uint8_t priority, os_task_handle_t* handle,
                                 uint8_t core_id)
{
    if (task_func == NULL) {
        return OS_INVALID_PARAM;
    }

    TaskHandle_t task_handle = NULL;
    
#if OS_HAS_CORE_AFFINITY
    // Multi-core system (ESP32, ESP32-S3) - use core pinning
    BaseType_t result = xTaskCreatePinnedToCore(
        (TaskFunction_t)task_func,
        name ? name : "unnamed_task",
        stack_size / sizeof(StackType_t),  // FreeRTOS expects stack size in words
        params,
        priority,
        &task_handle,
        core_id
    );
#else
    // Single-core system (STM32, ESP32-C3, etc.) - ignore core_id
    (void)core_id;  // Suppress unused parameter warning
    BaseType_t result = xTaskCreate(
        (TaskFunction_t)task_func,
        name ? name : "unnamed_task",
        stack_size / sizeof(StackType_t),
        params,
        priority,
        &task_handle
    );
#endif

    if (result != pdPASS) {
        LOG_E(TAG, "Failed to create pinned task: %s", name ? name : "unnamed");
        return (result == errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY) ? OS_NO_MEMORY : OS_ERROR;
    }

    if (handle != NULL) {
        *handle = (os_task_handle_t)task_handle;
    }

#if OS_HAS_CORE_AFFINITY
    LOG_D(TAG, "Created pinned task: %s, priority=%d, stack=%lu, core=%d", 
          name ? name : "unnamed", priority, stack_size, core_id);
#else
    LOG_D(TAG, "Created task: %s, priority=%d, stack=%lu (core pinning not supported)", 
          name ? name : "unnamed", priority, stack_size);
#endif
    return OS_SUCCESS;
}

void os_task_delete(os_task_handle_t handle)
{
    vTaskDelete((TaskHandle_t)handle);
    LOG_D(TAG, "Task deleted");
}

os_task_handle_t os_task_get_current(void)
{
    return (os_task_handle_t)xTaskGetCurrentTaskHandle();
}

// =============================================================================
// SYNCHRONIZATION - MUTEX
// =============================================================================

os_mutex_handle_t os_mutex_create(void)
{
    SemaphoreHandle_t mutex = xSemaphoreCreateMutex();
    if (mutex == NULL) {
        LOG_E(TAG, "Failed to create mutex");
        return NULL;
    }

    LOG_D(TAG, "Mutex created");
    return (os_mutex_handle_t)mutex;
}

void os_mutex_delete(os_mutex_handle_t mutex)
{
    if (mutex == NULL) {
        LOG_W(TAG, "Attempt to delete NULL mutex");
        return;
    }

    vSemaphoreDelete((SemaphoreHandle_t)mutex);
    LOG_D(TAG, "Mutex deleted");
}

os_result_t os_mutex_take(os_mutex_handle_t mutex, uint32_t timeout_ms)
{
    if (mutex == NULL) {
        return OS_INVALID_PARAM;
    }

    TickType_t timeout_ticks;
    if (timeout_ms == OS_WAIT_FOREVER) {
        timeout_ticks = portMAX_DELAY;
    } else if (timeout_ms == OS_NO_WAIT) {
        timeout_ticks = 0;
    } else {
        timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    }

    BaseType_t result = xSemaphoreTake((SemaphoreHandle_t)mutex, timeout_ticks);
    return (result == pdTRUE) ? OS_SUCCESS : OS_TIMEOUT;
}

os_result_t os_mutex_give(os_mutex_handle_t mutex)
{
    if (mutex == NULL) {
        return OS_INVALID_PARAM;
    }

    BaseType_t result = xSemaphoreGive((SemaphoreHandle_t)mutex);
    return (result == pdTRUE) ? OS_SUCCESS : OS_ERROR;
}

// =============================================================================
// SYNCHRONIZATION - SEMAPHORE
// =============================================================================

os_semaphore_handle_t os_semaphore_create_binary(void)
{
    SemaphoreHandle_t sem = xSemaphoreCreateBinary();
    if (sem == NULL) {
        LOG_E(TAG, "Failed to create binary semaphore");
        return NULL;
    }

    LOG_D(TAG, "Binary semaphore created");
    return (os_semaphore_handle_t)sem;
}

os_semaphore_handle_t os_semaphore_create_counting(uint32_t max_count, uint32_t initial_count)
{
    if (max_count == 0 || initial_count > max_count) {
        LOG_E(TAG, "Invalid semaphore parameters");
        return NULL;
    }

    SemaphoreHandle_t sem = xSemaphoreCreateCounting(max_count, initial_count);
    if (sem == NULL) {
        LOG_E(TAG, "Failed to create counting semaphore");
        return NULL;
    }

    LOG_D(TAG, "Counting semaphore created: max=%lu, initial=%lu", max_count, initial_count);
    return (os_semaphore_handle_t)sem;
}

void os_semaphore_delete(os_semaphore_handle_t semaphore)
{
    if (semaphore == NULL) {
        LOG_W(TAG, "Attempt to delete NULL semaphore");
        return;
    }

    vSemaphoreDelete((SemaphoreHandle_t)semaphore);
    LOG_D(TAG, "Semaphore deleted");
}

os_result_t os_semaphore_take(os_semaphore_handle_t semaphore, uint32_t timeout_ms)
{
    if (semaphore == NULL) {
        return OS_INVALID_PARAM;
    }

    TickType_t timeout_ticks;
    if (timeout_ms == OS_WAIT_FOREVER) {
        timeout_ticks = portMAX_DELAY;
    } else if (timeout_ms == OS_NO_WAIT) {
        timeout_ticks = 0;
    } else {
        timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    }

    BaseType_t result = xSemaphoreTake((SemaphoreHandle_t)semaphore, timeout_ticks);
    return (result == pdTRUE) ? OS_SUCCESS : OS_TIMEOUT;
}

os_result_t os_semaphore_give(os_semaphore_handle_t semaphore)
{
    if (semaphore == NULL) {
        return OS_INVALID_PARAM;
    }

    BaseType_t result = xSemaphoreGive((SemaphoreHandle_t)semaphore);
    return (result == pdTRUE) ? OS_SUCCESS : OS_ERROR;
}
os_result_t os_semaphore_give_from_isr(os_semaphore_handle_t semaphore, bool* higher_priority_task_woken)
{
    if (semaphore == NULL) {
        return OS_INVALID_PARAM;
    }

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    BaseType_t result = xSemaphoreGiveFromISR((SemaphoreHandle_t)semaphore, &xHigherPriorityTaskWoken);
    
    if (higher_priority_task_woken != NULL) {
        *higher_priority_task_woken = (xHigherPriorityTaskWoken == pdTRUE);
    }
    
    return (result == pdPASS) ? OS_SUCCESS : OS_ERROR;
}

os_result_t os_semaphore_take_from_isr(os_semaphore_handle_t semaphore, bool* higher_priority_task_woken)
{
    if (semaphore == NULL) {
        return OS_INVALID_PARAM;
    }

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    BaseType_t result = xSemaphoreTakeFromISR((SemaphoreHandle_t)semaphore, &xHigherPriorityTaskWoken);
    
    if (higher_priority_task_woken != NULL) {
        *higher_priority_task_woken = (xHigherPriorityTaskWoken == pdTRUE);
    }
    
    return (result == pdPASS) ? OS_SUCCESS : OS_TIMEOUT;
}

// =============================================================================
// ISR UTILITIES
// =============================================================================

void os_yield_from_isr(bool higher_priority_task_woken)
{
    portYIELD_FROM_ISR(higher_priority_task_woken ? pdTRUE : pdFALSE);
}

// =============================================================================
// TIME OPERATIONS
// =============================================================================

uint32_t os_get_tick_count(void)
{
    return (uint32_t)xTaskGetTickCount();
}

uint32_t os_get_time_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

void os_delay_ms(uint32_t delay_ms)
{
    if (delay_ms > 0) {
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

uint32_t os_ms_to_ticks(uint32_t ms)
{
    return (uint32_t)pdMS_TO_TICKS(ms);
}

uint32_t os_ticks_to_ms(uint32_t ticks)
{
    return (uint32_t)(ticks * portTICK_PERIOD_MS);
}