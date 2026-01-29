/**
 * @file hal_gpio_stm32.c
 * @brief STM32 implementation of GPIO HAL interface
 */

#include "hal_gpio.h"
#include "hal_gpio_interface.h"
#include "portable_log.h"
#include "stm32f7xx_hal.h"

static const char *TAG = "HAL_GPIO";

// ============================================================================
// STM32 GPIO Pin Encoding/Decoding
// ============================================================================

/**
 * @brief Decode hal_gpio_pin_t to STM32 port
 * Upper 16 bits contain the port address
 */
#define STM32_GPIO_GET_PORT(encoded)  ((GPIO_TypeDef*)((encoded) & 0xFFFF0000))

/**
 * @brief Decode hal_gpio_pin_t to STM32 pin mask
 * Lower 16 bits contain the pin mask
 */
#define STM32_GPIO_GET_PIN(encoded)   ((uint16_t)((encoded) & 0xFFFF))

// ============================================================================
// STM32 GPIO Implementation (Interface Pattern)
// ============================================================================

static bool stm32_gpio_init(void)
{
    LOG_I(TAG, "GPIO HAL initialized (STM32)");
    // GPIO clocks are enabled by STM32CubeMX generated code
    return true;
}

static bool stm32_gpio_config(hal_gpio_pin_t pin, hal_gpio_mode_t mode)
{
    GPIO_TypeDef* port = STM32_GPIO_GET_PORT(pin);
    uint16_t pin_mask = STM32_GPIO_GET_PIN(pin);

    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = pin_mask;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;

    switch (mode) {
        case HAL_GPIO_MODE_INPUT:
            GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
            GPIO_InitStruct.Pull = GPIO_NOPULL;
            break;

        case HAL_GPIO_MODE_OUTPUT:
            GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
            GPIO_InitStruct.Pull = GPIO_NOPULL;
            break;

        case HAL_GPIO_MODE_INPUT_PULLUP:
            GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
            GPIO_InitStruct.Pull = GPIO_PULLUP;
            break;

        case HAL_GPIO_MODE_INPUT_PULLDOWN:
            GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
            GPIO_InitStruct.Pull = GPIO_PULLDOWN;
            break;

        case HAL_GPIO_MODE_OUTPUT_OD:
            GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
            GPIO_InitStruct.Pull = GPIO_NOPULL;
            break;

        default:
            LOG_E(TAG, "Invalid GPIO mode");
            return false;
    }

    HAL_GPIO_Init(port, &GPIO_InitStruct);
    return true;
}

static bool stm32_gpio_set(hal_gpio_pin_t pin, hal_gpio_level_t level)
{
    GPIO_TypeDef* port = STM32_GPIO_GET_PORT(pin);
    uint16_t pin_mask = STM32_GPIO_GET_PIN(pin);

    HAL_GPIO_WritePin(port, pin_mask, (GPIO_PinState)level);
    return true;
}

static hal_gpio_level_t stm32_gpio_read(hal_gpio_pin_t pin)
{
    GPIO_TypeDef* port = STM32_GPIO_GET_PORT(pin);
    uint16_t pin_mask = STM32_GPIO_GET_PIN(pin);

    return (hal_gpio_level_t)HAL_GPIO_ReadPin(port, pin_mask);
}

static bool stm32_gpio_toggle(hal_gpio_pin_t pin)
{
    GPIO_TypeDef* port = STM32_GPIO_GET_PORT(pin);
    uint16_t pin_mask = STM32_GPIO_GET_PIN(pin);

    HAL_GPIO_TogglePin(port, pin_mask);
    return true;
}

static bool stm32_gpio_write(hal_gpio_pin_t pin, uint8_t state)
{
    return stm32_gpio_set(pin, state ? HAL_GPIO_LEVEL_HIGH : HAL_GPIO_LEVEL_LOW);
}

// ============================================================================
// STM32 GPIO Interface Registration
// ============================================================================

static const hal_gpio_interface_t stm32_gpio_impl = {
    .init = stm32_gpio_init,
    .config = stm32_gpio_config,
    .set = stm32_gpio_set,
    .read = stm32_gpio_read,
    .toggle = stm32_gpio_toggle,
    .write = stm32_gpio_write,
};

const hal_gpio_interface_t* hal_gpio = &stm32_gpio_impl;
