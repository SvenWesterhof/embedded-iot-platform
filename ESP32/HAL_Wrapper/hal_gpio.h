#ifndef HAL_GPIO_H
#define HAL_GPIO_H

#include <stdint.h>
#include <stdbool.h>
#include "hal_gpio_interface.h"

/**
 * @brief Initialize GPIO HAL
 * @return true if successful
 */
bool hal_gpio_init(void);

/**
 * @brief Configure a GPIO pin
 * @param pin GPIO pin number
 * @param mode Pin mode
 * @return true if successful
 */
bool hal_gpio_config(uint8_t pin, hal_gpio_mode_t mode);

/**
 * @brief Set GPIO pin level
 * @param pin GPIO pin number
 * @param level Level to set
 * @return true if successful
 */
bool hal_gpio_set(uint8_t pin, hal_gpio_level_t level);

/**
 * @brief Read GPIO pin level
 * @param pin GPIO pin number
 * @return Pin level
 */
hal_gpio_level_t hal_gpio_read(uint8_t pin);

/**
 * @brief Toggle GPIO pin
 * @param pin GPIO pin number
 * @return true if successful
 */
bool hal_gpio_toggle(uint8_t pin);

#endif // HAL_GPIO_H
