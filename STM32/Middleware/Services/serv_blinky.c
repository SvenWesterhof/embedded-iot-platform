#include "serv_blinky.h"
#include "bsp.h"
#include "hal_delay.h"

static uint32_t last_toggle = 0;
static uint32_t interval_ms = 5000;

void blinky_init(void)
{
    // Nothing to init for now, GPIO already initialized in MX_GPIO_Init
    last_toggle = hal_delay->get_tick_ms();
}

void blinky_run(void)
{
    uint32_t now = hal_delay->get_tick_ms();
    if ((now - last_toggle) >= interval_ms)
    {
        BSP_LED_Toggle();
        last_toggle = now;
    }
}
