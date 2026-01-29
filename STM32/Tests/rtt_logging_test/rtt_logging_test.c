/**
 * @file rtt_logging_test.c
 * @brief RTT logging functionality test and demonstration
 */

#include "SEGGER_RTT.h"
#include "portable_log.h"
#include "FreeRTOS.h"
#include "task.h"
#include "stm32f7xx_hal.h"
#include <stdio.h>

static const char *TAG = "RTT_TEST";

/**
 * @brief Test RTT logging functionality
 * 
 * This function demonstrates various RTT logging capabilities:
 * - Direct RTT calls
 * - Log macros (LOG_I, LOG_W, LOG_E)
 * - Formatted output
 * - Buffer output
 */
void rtt_test_logging(void)
{
    // Direct RTT output
    SEGGER_RTT_WriteString(0, "\n=== RTT Logging Test ===\n");
    
    // Test formatted RTT output using SEGGER_RTT_printf
    SEGGER_RTT_printf(0, "System uptime: %lu ms\n", HAL_GetTick());
    SEGGER_RTT_printf(0, "Free heap: %u bytes\n", xPortGetFreeHeapSize());
    
    // Test log macros with different levels
    LOG_I(TAG, "Info level log - RTT is working!");
    LOG_W(TAG, "Warning level log - with parameter: %d", 42);
    LOG_E(TAG, "Error level log - testing colors");
    
    // Test debug logs (might be compiled out)
    LOG_D(TAG, "Debug log - only if LOG_LEVEL_DEBUG is defined");
    LOG_V(TAG, "Verbose log - most detailed level");
    
    // Test buffer hex dump
    uint8_t test_buffer[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x12, 0x34, 0x56, 0x78};
    LOG_BUFFER_HEX(TAG, test_buffer, sizeof(test_buffer));
    
    SEGGER_RTT_WriteString(0, "=== RTT Test Complete ===\n\n");
}

/**
 * @brief RTT performance test
 * 
 * Measures RTT throughput and timing
 */
void rtt_test_performance(void)
{
    const char test_msg[] = "RTT Performance Test Message 1234567890\n";
    const uint32_t iterations = 1000;
    
    uint32_t start_time = HAL_GetTick();
    
    for (uint32_t i = 0; i < iterations; i++) {
        SEGGER_RTT_WriteString(0, test_msg);
    }
    
    uint32_t end_time = HAL_GetTick();
    uint32_t duration = end_time - start_time;
    
    SEGGER_RTT_printf(0, "RTT Performance: %lu messages in %lu ms (%.2f msg/sec)\n", 
                      iterations, duration, 
                      duration > 0 ? (float)iterations * 1000.0f / duration : 0.0f);
}

/**
 * @brief Continuous RTT output for testing
 * 
 * Call this from a task to generate periodic log output
 */
void rtt_test_continuous(void)
{
    static uint32_t counter = 0;
    
    LOG_I(TAG, "Continuous test #%lu - Free heap: %u bytes", 
          counter++, xPortGetFreeHeapSize());
    
    if (counter % 10 == 0) {
        LOG_W(TAG, "Checkpoint reached: %lu", counter);
    }
    
    if (counter % 50 == 0) {
        LOG_E(TAG, "Major checkpoint: %lu", counter);
        rtt_test_performance();
    }
}