#include "bsp.h"
#include "hal_gpio.h"
#include "main.h" // For hi2c2

extern I2C_HandleTypeDef hi2c2;
extern I2C_HandleTypeDef hi2c4;

/**
 * @brief Initialize the Board Support Package
 * 
 * This function performs board-level initialization.
 * Called early in the boot process.
 */
void BSP_Init(void)
{
    // Board-specific initialization
    // Note: HAL and system clocks are already initialized by SystemClock_Config()
    
    // Initialize board LEDs
    BSP_LED_Init();
    
    // Add other board-specific initialization here
    // - External oscillators
    // - Power management
    // - Board-specific peripherals
}

/**
 * @brief Initialize board LEDs
 */
void BSP_LED_Init(void)
{
    // GPIO for LED is initialized in MX_GPIO_Init() from Core/
    // This function can add additional LED-specific setup if needed
}

/**
 * @brief Turn on the board LED
 */
void BSP_LED_On(void)
{
    hal_gpio_write_pin((hal_gpio_port_t)EXT_LED_GPIO_PORT, EXT_LED_PIN, HAL_GPIO_PIN_SET);
}

/**
 * @brief Turn off the board LED
 */
void BSP_LED_Off(void)
{
    hal_gpio_write_pin((hal_gpio_port_t)EXT_LED_GPIO_PORT, EXT_LED_PIN, HAL_GPIO_PIN_RESET);
}

/**
 * @brief Toggle the board LED
 */
void BSP_LED_Toggle(void)
{
    hal_gpio_toggle_pin((hal_gpio_port_t)EXT_LED_GPIO_PORT, EXT_LED_PIN);
}

/**
 * @brief Get I2C handle for temperature sensor
 * @return I2C handle for temperature sensor peripheral
 */
hal_i2c_handle_t BSP_Get_TempSensor_I2C(void)
{
    return (hal_i2c_handle_t)&hi2c2;
}

/**
 * @brief Get I2C handle for current sensor (INA226)
 * @return I2C handle for INA226 peripheral
 */
hal_i2c_handle_t BSP_Get_CurrentSensor_I2C(void)
{
    return (hal_i2c_handle_t)&hi2c4;
}
