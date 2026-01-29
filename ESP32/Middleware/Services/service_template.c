/**
 * @file service_template.c
 * @brief Template for creating shared services in the Middleware layer
 * 
 * INSTRUCTIONS:
 * 1. Copy this file and rename to svc_<your_service>.c
 * 2. Services are stateless utility functions shared by features
 * 3. Add to Middleware/CMakeLists.txt SRCS list
 */

#include "service_template.h"
#include <esp_log.h>

static const char *TAG = "SERVICE_TEMPLATE";

/**
 * @brief Initialize service (if needed)
 */
bool service_template_init(void)
{
    ESP_LOGI(TAG, "Service initialized");
    
    // TODO: Initialize service resources if needed
    
    return true;
}

/**
 * @brief Example service function
 */
bool service_template_process(void *input, void *output)
{
    if (!input || !output) {
        ESP_LOGE(TAG, "Invalid parameters");
        return false;
    }
    
    // TODO: Implement service logic
    
    return true;
}
