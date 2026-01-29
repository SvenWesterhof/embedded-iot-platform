#include "hal_delay.h"
#include "../OS/os_wrapper.h"
#include <esp_timer.h>
#include <esp_rom_sys.h>

void hal_delay_ms(uint32_t ms)
{
    os_delay_ms(ms);
}

void hal_delay_us(uint32_t us)
{
    esp_rom_delay_us(us);
}

uint32_t hal_get_tick_ms(void)
{
    return os_get_time_ms();
}
