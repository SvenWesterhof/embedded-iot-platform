/**
 * @file serv_uart_test.h
 * @brief UART test service for logic analyzer verification
 */

#ifndef SERV_UART_TEST_H
#define SERV_UART_TEST_H

#include <stdbool.h>

/**
 * @brief Initialize UART test service
 * @return true if successful, false otherwise
 */
bool serv_uart_test_init(void);

/**
 * @brief Send test pattern using blocking transmission
 *
 * Sends a comprehensive test pattern including:
 * - ASCII text "UART_TEST_" repeated 5 times
 * - Bit patterns (0x55, 0xAA) for baud rate verification
 * - Min/max values (0x00, 0xFF)
 * - Counting sequence (0x00 to 0xFF)
 *
 * Expected on Logic Analyzer (115200 baud, 8N1):
 * - Clear ASCII text pattern
 * - Alternating bit patterns
 * - Full counting sequence
 */
void serv_uart_test_send_pattern_blocking(void);

/**
 * @brief Send test pattern using async DMA transmission
 *
 * Same as blocking version but uses non-blocking DMA transmission.
 * Requires TX DMA to be configured in CubeMX.
 */
void serv_uart_test_send_pattern_async(void);

/**
 * @brief Continuous test loop
 *
 * Sends test pattern every 2 seconds.
 * Call this from your main task loop for continuous monitoring.
 */
void serv_uart_test_loop(void);

#endif // SERV_UART_TEST_H
