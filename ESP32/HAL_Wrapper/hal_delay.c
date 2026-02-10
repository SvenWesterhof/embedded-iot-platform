#include "hal_delay.h"
#include "hal_delay_interface.h"
#include "os_wrapper.h"
#include <esp_timer.h>
#include <esp_rom_sys.h>

// ============================================================================
// ESP32 Delay Implementation (Interface Pattern)
// ============================================================================

static bool esp32_delay_init(void)
{
    // ESP32 timers are initialized by esp_timer_init() in startup
    return true;
}

static void esp32_delay_ms(uint32_t ms)
{
    os_delay_ms(ms);
}

static void esp32_delay_us(uint32_t us)
{
    esp_rom_delay_us(us);
}

static uint32_t esp32_get_tick_ms(void)
{
    return os_get_time_ms();
}

static uint32_t esp32_get_tick_us(void)
{
    return (uint32_t)(esp_timer_get_time());
}

static uint32_t esp32_elapsed_ms(uint32_t start_tick)
{
    uint32_t now = esp32_get_tick_ms();
    // Handle 32-bit wrap-around
    return (now >= start_tick) ? (now - start_tick) : (0xFFFFFFFF - start_tick + now + 1);
}

static bool esp32_is_timeout(uint32_t start_tick, uint32_t timeout_ms)
{
    return esp32_elapsed_ms(start_tick) >= timeout_ms;
}

// ============================================================================
// ESP32 Delay Interface Registration
// ============================================================================

static const hal_delay_interface_t esp32_delay_impl = {
    .init = esp32_delay_init,
    .delay_ms = esp32_delay_ms,
    .delay_us = esp32_delay_us,
    .get_tick_ms = esp32_get_tick_ms,
    .get_tick_us = esp32_get_tick_us,
    .elapsed_ms = esp32_elapsed_ms,
    .is_timeout = esp32_is_timeout,
};

const hal_delay_interface_t* hal_delay = &esp32_delay_impl;

// ============================================================================
// Backward Compatibility Functions (Old API)
// ============================================================================

void hal_delay_ms(uint32_t ms)
{
    esp32_delay_ms(ms);
}

void hal_delay_us(uint32_t us)
{
    esp32_delay_us(us);
}

uint32_t hal_get_tick_ms(void)
{
    return esp32_get_tick_ms();
}
