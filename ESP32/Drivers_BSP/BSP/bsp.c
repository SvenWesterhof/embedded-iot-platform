/**
 * @file bsp.c
 * @brief Board Support Package - Hardware initialization
 * 
 * Initialize all board-specific hardware for the ESP32 Gateway:
 * - GPIO configuration (LED, STM32 handshake pins)
 * - UART for STM32 communication
 */

#include "bsp.h"
#include "pinout.h"
#include "../../HAL_Wrapper/hal_gpio.h"
#include "../../HAL_Wrapper/hal_uart.h"
#include "../Drivers_BSP/Custom/portable_log.h"

static const char *TAG = "BSP";

// Track LED state for toggle function
static bool led_state = false;

/**
 * @brief Initialize GPIO pins
 */
static bool bsp_gpio_init(void)
{
    LOG_I(TAG, "Initializing GPIO...");
    
    // Initialize GPIO HAL
    if (!hal_gpio_init()) {
        LOG_E(TAG, "Failed to initialize GPIO HAL");
        return false;
    }
    
    // Configure LED as output
    if (!hal_gpio_config(LED_BUILTIN, HAL_GPIO_MODE_OUTPUT)) {
        LOG_E(TAG, "Failed to configure LED pin");
        return false;
    }
    hal_gpio_set(LED_BUILTIN, HAL_GPIO_LEVEL_LOW);
    
    // Configure STM32 wakeup pin as output (active low by default)
    if (!hal_gpio_config(STM32_WAKEUP_PIN, HAL_GPIO_MODE_OUTPUT)) {
        LOG_E(TAG, "Failed to configure STM32 wakeup pin");
        return false;
    }
    hal_gpio_set(STM32_WAKEUP_PIN, HAL_GPIO_LEVEL_LOW);
    
    // Configure STM32 ready pin as input with pull-down
    if (!hal_gpio_config(STM32_READY_PIN, HAL_GPIO_MODE_INPUT_PULLDOWN)) {
        LOG_E(TAG, "Failed to configure STM32 ready pin");
        return false;
    }
    
    LOG_I(TAG, "GPIO initialized");
    return true;
}

/**
 * @brief Initialize board support package
 */
bool bsp_init(void)
{
    LOG_I(TAG, "Initializing BSP...");
    
    // Initialize GPIO pins
    if (!bsp_gpio_init()) {
        LOG_E(TAG, "GPIO initialization failed");
        return false;
    }
    
    // Note: UART initialization is handled by stm32_uart_driver
    // to keep the driver self-contained and configurable
    
    LOG_I(TAG, "BSP initialized successfully");
    return true;
}

/**
 * @brief Deinitialize board support package
 */
bool bsp_deinit(void)
{
    LOG_I(TAG, "Deinitializing BSP...");
    
    // Turn off LED
    hal_gpio_set(LED_BUILTIN, HAL_GPIO_LEVEL_LOW);
    
    // Deassert wakeup
    hal_gpio_set(STM32_WAKEUP_PIN, HAL_GPIO_LEVEL_LOW);
    
    LOG_I(TAG, "BSP deinitialized");
    return true;
}

/**
 * @brief Set STM32 wakeup pin state
 */
void bsp_stm32_wakeup_set(bool active)
{
    hal_gpio_set(STM32_WAKEUP_PIN, active ? HAL_GPIO_LEVEL_HIGH : HAL_GPIO_LEVEL_LOW);
}

/**
 * @brief Check if STM32 is ready
 */
bool bsp_stm32_is_ready(void)
{
    return (hal_gpio_read(STM32_READY_PIN) == HAL_GPIO_LEVEL_HIGH);
}

/**
 * @brief Set LED state
 */
void bsp_led_set(bool on)
{
    led_state = on;
    hal_gpio_set(LED_BUILTIN, on ? HAL_GPIO_LEVEL_HIGH : HAL_GPIO_LEVEL_LOW);
}

/**
 * @brief Toggle LED state
 */
void bsp_led_toggle(void)
{
    led_state = !led_state;
    hal_gpio_set(LED_BUILTIN, led_state ? HAL_GPIO_LEVEL_HIGH : HAL_GPIO_LEVEL_LOW);
}
