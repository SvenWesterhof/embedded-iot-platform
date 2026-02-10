/**
 * @file hal_gpio_interface.h
 * @brief Hardware Abstraction Layer interface for GPIO
 *
 * This file defines the GPIO HAL interface using the Strategy pattern.
 * Each platform implements this interface with platform-specific code.
 *
 * Usage:
 *   hal_gpio->init();
 *   hal_gpio->config(LED_PIN, HAL_GPIO_MODE_OUTPUT);
 *   hal_gpio->set(LED_PIN, HAL_GPIO_LEVEL_HIGH);
 */

#ifndef HAL_GPIO_INTERFACE_H
#define HAL_GPIO_INTERFACE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// GPIO Types and Enums (Platform-Independent)
// ============================================================================

/**
 * @brief GPIO pin mode
 */
typedef enum {
    HAL_GPIO_MODE_INPUT,            /**< Input mode (floating) */
    HAL_GPIO_MODE_OUTPUT,           /**< Output mode (push-pull) */
    HAL_GPIO_MODE_INPUT_PULLUP,     /**< Input with pull-up */
    HAL_GPIO_MODE_INPUT_PULLDOWN,   /**< Input with pull-down */
    HAL_GPIO_MODE_OUTPUT_OD,        /**< Output open-drain */
} hal_gpio_mode_t;

/**
 * @brief GPIO pin level
 */
typedef enum {
    HAL_GPIO_LEVEL_LOW = 0,         /**< Logic low (0V) */
    HAL_GPIO_LEVEL_HIGH = 1         /**< Logic high (3.3V/5V) */
} hal_gpio_level_t;

/**
 * @brief GPIO pin handle (platform-specific internal representation)
 *
 * - ESP32: Simple pin number (0-39)
 * - STM32: Encoded port+pin (e.g., (GPIOA << 16) | GPIO_PIN_5)
 */
typedef uint32_t hal_gpio_pin_t;

// ============================================================================
// GPIO HAL Interface (Strategy Pattern)
// ============================================================================

/**
 * @brief GPIO Hardware Abstraction Layer interface
 *
 * This structure defines function pointers for all GPIO operations.
 * Each platform provides its own implementation.
 */
typedef struct {
    /**
     * @brief Initialize GPIO HAL
     * @return true if successful, false otherwise
     */
    bool (*init)(void);

    /**
     * @brief Configure a GPIO pin
     * @param pin GPIO pin handle
     * @param mode Pin mode (input/output/pull-up/pull-down)
     * @return true if successful, false otherwise
     */
    bool (*config)(hal_gpio_pin_t pin, hal_gpio_mode_t mode);

    /**
     * @brief Set GPIO pin level
     * @param pin GPIO pin handle
     * @param level Level to set (high/low)
     * @return true if successful, false otherwise
     */
    bool (*set)(hal_gpio_pin_t pin, hal_gpio_level_t level);

    /**
     * @brief Read GPIO pin level
     * @param pin GPIO pin handle
     * @return Current pin level (high/low)
     */
    hal_gpio_level_t (*read)(hal_gpio_pin_t pin);

    /**
     * @brief Toggle GPIO pin
     * @param pin GPIO pin handle
     * @return true if successful, false otherwise
     */
    bool (*toggle)(hal_gpio_pin_t pin);

    /**
     * @brief Write GPIO pin (convenience wrapper for set)
     * @param pin GPIO pin handle
     * @param state State to write (0 or 1)
     * @return true if successful, false otherwise
     */
    bool (*write)(hal_gpio_pin_t pin, uint8_t state);

} hal_gpio_interface_t;

// ============================================================================
// Global HAL Instance
// ============================================================================

/**
 * @brief Global GPIO HAL interface pointer
 *
 * This pointer is set by the platform-specific implementation during
 * initialization. Application code uses this to access GPIO functions.
 *
 * Example:
 *   hal_gpio->set(LED_PIN, HAL_GPIO_LEVEL_HIGH);
 */
extern const hal_gpio_interface_t* hal_gpio;

// ============================================================================
// Usage Notes
// ============================================================================
//
// New code should use the interface directly:
//   hal_gpio->init();
//   hal_gpio->config(pin, mode);
//   hal_gpio->set(pin, level);
//   hal_gpio->read(pin);
//   hal_gpio->toggle(pin);
//

#ifdef __cplusplus
}
#endif

#endif // HAL_GPIO_INTERFACE_H
