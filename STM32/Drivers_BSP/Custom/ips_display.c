
#include <pinout.h>
#include <stdio.h>
#include "ips_display.h"
#include "st7735.h"
#include "hal_gpio.h"

static bool display_initialized = false;

ips_display_status_t ips_display_init() {
    // Basic driver initialization
    display_initialized = false;
    return IPS_DISPLAY_OK;
}

ips_display_status_t ips_display_open() {
    if (display_initialized) {
        return IPS_DISPLAY_ERROR; // Already open
    }

    // Initialize ST7735 hardware
    ST7735_Init();
    
    // Fill background
    ST7735_FillScreen(ST7735_BLACK);

    // Draw static labels
    ST7735_WriteString(10, 10, "Temp: ", Font_11x18, ST7735_WHITE, ST7735_BLACK);
    ST7735_WriteString(10, 40, "Hum:  ", Font_11x18, ST7735_WHITE, ST7735_BLACK);
    
    // Enable backlight
    hal_gpio_write_pin((hal_gpio_port_t)DISPLAY_BACKLIGHT_PORT, DISPLAY_BACKLIGHT_PIN, HAL_GPIO_PIN_SET);
    
    display_initialized = true;
    return IPS_DISPLAY_OK;
}

ips_display_status_t ips_display_close() {
    if (!display_initialized) {
        return IPS_DISPLAY_ERROR;
    }
    
    // Disable backlight
    hal_gpio_write_pin((hal_gpio_port_t)DISPLAY_BACKLIGHT_PORT, DISPLAY_BACKLIGHT_PIN, HAL_GPIO_PIN_RESET);
    
    // Put display in sleep mode and turn off
    ST7735_Sleep();
    
    display_initialized = false;
    return IPS_DISPLAY_OK;
}

void ips_display_deinit() {
    if (display_initialized) {
        ips_display_close();
    }
}

ips_display_status_t ips_display_write_temp_data(float temperature, float humidity) {
    if (!display_initialized) {
        return IPS_DISPLAY_ERROR;
    }
    
    char temp_val[16];
    char hum_val[16];

    // Format the numeric values using floats
    snprintf(temp_val, sizeof(temp_val), "%.2f C", temperature);
    snprintf(hum_val, sizeof(hum_val), "%.2f %%", humidity);

    // Overwrite only the numeric values on the display
    ST7735_WriteString(80, 10, temp_val, Font_11x18, ST7735_WHITE, ST7735_BLACK);
    ST7735_WriteString(80, 40, hum_val, Font_11x18, ST7735_WHITE, ST7735_BLACK);

    return IPS_DISPLAY_OK;
}
