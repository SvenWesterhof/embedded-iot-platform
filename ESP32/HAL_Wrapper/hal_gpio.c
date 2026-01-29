#include "hal_gpio.h"
#include "hal_gpio_interface.h"
#include <driver/gpio.h>
#include "portable_log.h"

static const char *TAG = "HAL_GPIO";

// ============================================================================
// ESP32 GPIO Implementation (Interface Pattern)
// ============================================================================

static bool esp32_gpio_init(void)
{
    LOG_I(TAG, "Initializing GPIO HAL (ESP32)");
    return true;
}

static bool esp32_gpio_config(hal_gpio_pin_t pin, hal_gpio_mode_t mode)
{
    gpio_config_t io_conf = {0};
    io_conf.pin_bit_mask = (1ULL << pin);

    switch (mode) {
        case HAL_GPIO_MODE_INPUT:
            io_conf.mode = GPIO_MODE_INPUT;
            io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
            io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
            break;

        case HAL_GPIO_MODE_OUTPUT:
            io_conf.mode = GPIO_MODE_OUTPUT;
            io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
            io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
            break;

        case HAL_GPIO_MODE_INPUT_PULLUP:
            io_conf.mode = GPIO_MODE_INPUT;
            io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
            io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
            break;

        case HAL_GPIO_MODE_INPUT_PULLDOWN:
            io_conf.mode = GPIO_MODE_INPUT;
            io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
            io_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;
            break;

        case HAL_GPIO_MODE_OUTPUT_OD:
            io_conf.mode = GPIO_MODE_OUTPUT_OD;
            io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
            io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
            break;

        default:
            LOG_E(TAG, "Invalid GPIO mode");
            return false;
    }

    io_conf.intr_type = GPIO_INTR_DISABLE;

    if (gpio_config(&io_conf) != ESP_OK) {
        LOG_E(TAG, "Failed to configure GPIO %lu", (unsigned long)pin);
        return false;
    }

    return true;
}

static bool esp32_gpio_set(hal_gpio_pin_t pin, hal_gpio_level_t level)
{
    return gpio_set_level((gpio_num_t)pin, level) == ESP_OK;
}

static hal_gpio_level_t esp32_gpio_read(hal_gpio_pin_t pin)
{
    return (hal_gpio_level_t)gpio_get_level((gpio_num_t)pin);
}

static bool esp32_gpio_toggle(hal_gpio_pin_t pin)
{
    hal_gpio_level_t current = esp32_gpio_read(pin);
    return esp32_gpio_set(pin, current == HAL_GPIO_LEVEL_HIGH ? HAL_GPIO_LEVEL_LOW : HAL_GPIO_LEVEL_HIGH);
}

static bool esp32_gpio_write(hal_gpio_pin_t pin, uint8_t state)
{
    return esp32_gpio_set(pin, state ? HAL_GPIO_LEVEL_HIGH : HAL_GPIO_LEVEL_LOW);
}

// ============================================================================
// ESP32 GPIO Interface Registration
// ============================================================================

static const hal_gpio_interface_t esp32_gpio_impl = {
    .init = esp32_gpio_init,
    .config = esp32_gpio_config,
    .set = esp32_gpio_set,
    .read = esp32_gpio_read,
    .toggle = esp32_gpio_toggle,
    .write = esp32_gpio_write,
};

const hal_gpio_interface_t* hal_gpio = &esp32_gpio_impl;

// ============================================================================
// Backward Compatibility Functions (Old API)
// ============================================================================

bool hal_gpio_init(void)
{
    return esp32_gpio_init();
}

bool hal_gpio_config(uint8_t pin, hal_gpio_mode_t mode)
{
    return esp32_gpio_config((hal_gpio_pin_t)pin, mode);
}

bool hal_gpio_set(uint8_t pin, hal_gpio_level_t level)
{
    return esp32_gpio_set((hal_gpio_pin_t)pin, level);
}

hal_gpio_level_t hal_gpio_read(uint8_t pin)
{
    return esp32_gpio_read((hal_gpio_pin_t)pin);
}

bool hal_gpio_toggle(uint8_t pin)
{
    return esp32_gpio_toggle((hal_gpio_pin_t)pin);
}
