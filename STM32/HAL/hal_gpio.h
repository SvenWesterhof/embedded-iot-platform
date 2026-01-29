#ifndef HAL_GPIO_H
#define HAL_GPIO_H

/**
 * @file hal_gpio.h
 * @brief STM32 GPIO HAL - now uses interface pattern
 *
 * This file provides backward compatibility with the old STM32 GPIO API
 * while also exposing the new hal_gpio interface.
 *
 * New code should use: hal_gpio->set(pin, level)
 * Old code can still use: hal_gpio_write_pin(port, pin, state)
 */

#include "hal_gpio_interface.h"
#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// Legacy STM32 API (Backward Compatibility)
// ============================================================================

// Legacy types (for backward compatibility with existing code)
typedef void* hal_gpio_port_t;

// Legacy GPIO Pin States
typedef enum {
    HAL_GPIO_PIN_RESET = 0,
    HAL_GPIO_PIN_SET = 1
} hal_gpio_pin_state_t;

// Legacy GPIO Functions (still supported for old code)
void hal_gpio_write_pin(hal_gpio_port_t port, uint16_t pin, hal_gpio_pin_state_t state);
hal_gpio_pin_state_t hal_gpio_read_pin(hal_gpio_port_t port, uint16_t pin);
void hal_gpio_toggle_pin(hal_gpio_port_t port, uint16_t pin);

#endif // HAL_GPIO_H
