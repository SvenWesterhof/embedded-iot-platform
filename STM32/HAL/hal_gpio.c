#include "hal_gpio.h"

// STM32-specific implementation
#include "stm32f7xx_hal.h"

/**
 * @brief Write to a GPIO pin
 * @param port GPIO port handle
 * @param pin GPIO pin number
 * @param state Pin state (RESET or SET)
 */
void hal_gpio_write_pin(hal_gpio_port_t port, hal_gpio_pin_t pin, hal_gpio_pin_state_t state)
{
    HAL_GPIO_WritePin((GPIO_TypeDef*)port, pin, (GPIO_PinState)state);
}

/**
 * @brief Read from a GPIO pin
 * @param port GPIO port handle
 * @param pin GPIO pin number
 * @return Current pin state
 */
hal_gpio_pin_state_t hal_gpio_read_pin(hal_gpio_port_t port, hal_gpio_pin_t pin)
{
    return (hal_gpio_pin_state_t)HAL_GPIO_ReadPin((GPIO_TypeDef*)port, pin);
}

/**
 * @brief Toggle a GPIO pin
 * @param port GPIO port handle
 * @param pin GPIO pin number
 */
void hal_gpio_toggle_pin(hal_gpio_port_t port, hal_gpio_pin_t pin)
{
    HAL_GPIO_TogglePin((GPIO_TypeDef*)port, pin);
}
