#include "hal_gpio.h"
#include <driver/gpio.h>
#include "../Drivers_BSP/Custom/portable_log.h"

static const char *TAG = "HAL_GPIO";

bool hal_gpio_init(void)
{
    LOG_I(TAG, "Initializing GPIO HAL");
    return true;
}

bool hal_gpio_config(uint8_t pin, hal_gpio_mode_t mode)
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
            
        default:
            LOG_E(TAG, "Invalid GPIO mode");
            return false;
    }
    
    io_conf.intr_type = GPIO_INTR_DISABLE;
    
    if (gpio_config(&io_conf) != ESP_OK) {
        LOG_E(TAG, "Failed to configure GPIO %d", pin);
        return false;
    }
    
    return true;
}

bool hal_gpio_set(uint8_t pin, hal_gpio_level_t level)
{
    return gpio_set_level(pin, level) == ESP_OK;
}

hal_gpio_level_t hal_gpio_read(uint8_t pin)
{
    return (hal_gpio_level_t)gpio_get_level(pin);
}

bool hal_gpio_toggle(uint8_t pin)
{
    hal_gpio_level_t current = hal_gpio_read(pin);
    return hal_gpio_set(pin, current == HAL_GPIO_LEVEL_HIGH ? HAL_GPIO_LEVEL_LOW : HAL_GPIO_LEVEL_HIGH);
}
