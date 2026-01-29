#ifndef HAL_GPIO_H
#define HAL_GPIO_H

/**
 * @file hal_gpio.h
 * @brief Platform-independent GPIO abstraction layer
 * 
 * This HAL provides a consistent GPIO API that can be ported to different platforms.
 * Implementation wraps STM32 HAL but can be replaced for other MCUs.
 */

#include <stdint.h>
#include <stdbool.h>

// GPIO Port/Pin Types (platform-independent)
typedef void* hal_gpio_port_t;
typedef uint16_t hal_gpio_pin_t;

// GPIO Pin States
typedef enum {
    HAL_GPIO_PIN_RESET = 0,
    HAL_GPIO_PIN_SET = 1
} hal_gpio_pin_state_t;

// GPIO Functions
void hal_gpio_write_pin(hal_gpio_port_t port, hal_gpio_pin_t pin, hal_gpio_pin_state_t state);
hal_gpio_pin_state_t hal_gpio_read_pin(hal_gpio_port_t port, hal_gpio_pin_t pin);
void hal_gpio_toggle_pin(hal_gpio_port_t port, hal_gpio_pin_t pin);

#endif // HAL_GPIO_H
