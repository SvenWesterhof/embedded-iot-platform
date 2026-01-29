#include <stdio.h>
#include "../Drivers_BSP/Custom/portable_log.h"
#include "app_main.h"
#include "uart_test.h"

// Set to 1 to run UART test, 0 for normal operation
#define UART_TEST_MODE 0

static const char *TAG = "MAIN";

void app_main(void)
{
    LOG_I(TAG, "ESP32 Gateway starting...");

#if UART_TEST_MODE
    LOG_I(TAG, "*** UART TEST MODE ***");
    uart_test_run();
#else
    if (app_init()) {
        app_run();
    } else {
        LOG_E(TAG, "Application init failed!");
    }
#endif
}
