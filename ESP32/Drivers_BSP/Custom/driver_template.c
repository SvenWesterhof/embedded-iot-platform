/**
 * @file driver_template.c
 * @brief Template for creating device drivers in the Drivers_BSP layer
 * 
 * INSTRUCTIONS:
 * 1. Copy this file and rename to <device_name>.c
 * 2. Implement hardware-specific driver logic
 * 3. Use HAL_Wrapper for all hardware access
 * 4. Add to Drivers_BSP/CMakeLists.txt SRCS list
 */

#include "driver_template.h"
#include "../BSP/pinout.h"
#include "../../HAL_Wrapper/hal_gpio.h"
#include "../../HAL_Wrapper/hal_i2c.h"
#include "../../HAL_Wrapper/hal_spi.h"
#include <esp_log.h>

static const char *TAG = "DRIVER_TEMPLATE";

// Driver state
typedef struct {
    bool initialized;
    // Add your device handle here
    // hal_i2c_dev_handle_t i2c_dev;
    // hal_spi_dev_handle_t spi_dev;
} driver_state_t;

static driver_state_t state = {0};

/**
 * @brief Initialize device driver
 */
bool driver_template_init(void)
{
    if (state.initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return true;
    }
    
    ESP_LOGI(TAG, "Initializing driver...");
    
    // TODO: Initialize hardware via HAL
    // Example I2C:
    // state.i2c_dev = hal_i2c_add_device(I2C_NUM_0, DEVICE_I2C_ADDR, 400000);
    
    // Example GPIO:
    // hal_gpio_set_direction(DEVICE_PIN, HAL_GPIO_MODE_OUTPUT);
    
    // TODO: Perform device-specific initialization
    // Example: Read device ID, configure registers
    
    state.initialized = true;
    ESP_LOGI(TAG, "Driver initialized");
    
    return true;
}

/**
 * @brief Deinitialize device driver
 */
void driver_template_deinit(void)
{
    if (!state.initialized) {
        return;
    }
    
    ESP_LOGI(TAG, "Deinitializing driver...");
    
    // TODO: Cleanup resources
    
    state.initialized = false;
}

/**
 * @brief Read data from device
 */
bool driver_template_read(uint8_t *data, size_t len)
{
    if (!state.initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return false;
    }
    
    if (!data) {
        ESP_LOGE(TAG, "Invalid parameter");
        return false;
    }
    
    // TODO: Implement read operation via HAL
    // Example I2C:
    // return hal_i2c_read(state.i2c_dev, data, len);
    
    return true;
}

/**
 * @brief Write data to device
 */
bool driver_template_write(const uint8_t *data, size_t len)
{
    if (!state.initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return false;
    }
    
    if (!data) {
        ESP_LOGE(TAG, "Invalid parameter");
        return false;
    }
    
    // TODO: Implement write operation via HAL
    // Example I2C:
    // return hal_i2c_write(state.i2c_dev, data, len);
    
    return true;
}

/**
 * @brief Perform device-specific operation
 */
bool driver_template_do_something(void)
{
    if (!state.initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return false;
    }
    
    // TODO: Implement device-specific functionality
    
    return true;
}
