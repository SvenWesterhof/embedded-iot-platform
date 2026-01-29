#include "services.h"
#include "serv_blinky.h"
#include "serv_temperature_sensor.h"
#include "serv_display.h"
#include "serv_current_monitor.h"
#include "protocol_handler.h"
#include "portable_log.h"
// /#include "hal_uart.h"
#include "os_wrapper.h"

#ifdef ENABLE_UART_TEST
#include "../../Tests/uart_test/serv_uart_test.h"
#endif

static const char *TAG = "SERVICES";
static uint32_t last_isr_log_time = 0;

void services_init(void)
{

    blinky_init();
    LOG_I(TAG, "Blinky initialized\n");

    temperature_sensor_init();
    LOG_I(TAG, "Temperature sensor initialized\n");
    
    display_init();
    LOG_I(TAG, "Display initialized\n");

    current_monitor_init();
    LOG_I(TAG, "Current monitor initialized\n");

    protocol_handler_init();
    LOG_I(TAG, "Protocol handler initialized\n");

#ifdef ENABLE_UART_TEST
    SEGGER_RTT_printf(0, "Services: Initializing UART test...\n");
    serv_uart_test_init();
    LOG_I(TAG, "UART test service initialized\n");
#endif
}

void services_run(void)
{
    blinky_run();
    temperature_sensor_run();
    display_run();
    current_monitor_process();

#ifdef ENABLE_UART_TEST
    serv_uart_test_loop();
#endif

    // Debug: Log ISR counters every 5 seconds
    // TEMPORARILY DISABLED - testing for boot loop

    uint32_t current_time = os_get_tick_count();
    if (os_ticks_to_ms(current_time - last_isr_log_time) >= 5000) {
        uint32_t idle_count, dma_ht_count, dma_tc_count;
        hal_uart_get_isr_counters(&idle_count, &dma_ht_count, &dma_tc_count);
        LOG_I(TAG, "UART ISR counters: IDLE=%lu, DMA_HT=%lu, DMA_TC=%lu",
              idle_count, dma_ht_count, dma_tc_count);
        last_isr_log_time = current_time;
    }

}
