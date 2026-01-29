/**
 * @file feature_template.c
 * @brief Template for creating new features in the Middleware layer
 * 
 * INSTRUCTIONS:
 * 1. Copy this file and rename to feat_<your_feature>.c
 * 2. Implement initialization, task, and callbacks
 * 3. Add to Middleware/CMakeLists.txt SRCS list
 * 4. Register in OS/os_tasks.c
 */

#include "feature_template.h"
#include "../../OS/event_bus.h"
#include "../../Drivers_BSP/BSP/bsp.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char *TAG = "FEATURE_TEMPLATE";

// Feature state
typedef struct {
    bool initialized;
    bool running;
    // Add your state variables here
} feature_state_t;

static feature_state_t state = {0};

/**
 * @brief Initialize feature
 */
bool feature_template_init(void)
{
    if (state.initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return true;
    }
    
    ESP_LOGI(TAG, "Initializing feature...");
    
    // TODO: Initialize hardware/resources
    // TODO: Subscribe to events
    // event_bus_subscribe(EVENT_YOUR_EVENT, feature_template_event_handler);
    
    state.initialized = true;
    ESP_LOGI(TAG, "Feature initialized");
    
    return true;
}

/**
 * @brief Start feature (create tasks if needed)
 */
bool feature_template_start(void)
{
    if (!state.initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return false;
    }
    
    if (state.running) {
        ESP_LOGW(TAG, "Already running");
        return true;
    }
    
    ESP_LOGI(TAG, "Starting feature...");
    
    // TODO: Create FreeRTOS task if needed
    // xTaskCreate(feature_template_task, "feature_task", 4096, NULL, 5, NULL);
    
    state.running = true;
    ESP_LOGI(TAG, "Feature started");
    
    return true;
}

/**
 * @brief Stop feature
 */
void feature_template_stop(void)
{
    ESP_LOGI(TAG, "Stopping feature...");
    
    // TODO: Stop tasks
    // TODO: Cleanup resources
    
    state.running = false;
    ESP_LOGI(TAG, "Feature stopped");
}

/**
 * @brief Feature task (if needed)
 */
static void feature_template_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Task started");
    
    while (1) {
        // TODO: Implement task logic
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/**
 * @brief Event handler for feature
 */
static void feature_template_event_handler(event_type_t event, void *data)
{
    ESP_LOGI(TAG, "Received event: %d", event);
    
    switch (event) {
        // TODO: Handle events
        default:
            break;
    }
}

/**
 * @brief Public API - example function
 */
bool feature_template_do_something(void)
{
    if (!state.initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return false;
    }
    
    // TODO: Implement functionality
    
    return true;
}
