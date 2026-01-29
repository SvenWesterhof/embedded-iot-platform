/**
 * @file serv_uart_test.c
 * @brief UART test service for logic analyzer verification
 *
 * This service sends recognizable patterns over UART for verification
 * with a logic analyzer.
 *
 * Test Pattern:
 * 1. ASCII "UART_TEST_" repeated 5 times
 * 2. 0x55 (0b01010101) - alternating bits for baud rate verification
 * 3. 0xAA (0b10101010) - alternating bits (opposite pattern)
 * 4. 0x00, 0xFF - min/max values
 * 5. Counting sequence 0x00 to 0xFF
 *
 * Logic Analyzer Setup:
 * - Channel: USART2 TX (PA2 or check your pinout)
 * - Baud Rate: 115200
 * - Format: 8 data bits, No parity, 1 stop bit
 * - Voltage: 3.3V logic level
 */

#include "serv_uart_test.h"
#include "../../HAL/hal_uart.h"
#include "../../OS/os_wrapper.h"
#include "../../Drivers_BSP/Custom/portable_log.h"
#include <string.h>

static const char *TAG = "UART_TEST";

// Static buffer for async transmission (must persist until TX_DONE)
static uint8_t async_tx_buffer[512];
static volatile bool async_tx_ready = true;

/**
 * @brief UART event callback for async tests
 */
static void uart_test_event_callback(hal_uart_port_t port, hal_uart_event_t *event, void *user_data)
{
    (void)user_data;

    if (port != HAL_UART_PORT_1) {
        return;
    }

    switch (event->type) {
        case HAL_UART_EVENT_TX_DONE:
            LOG_I(TAG, "Async TX complete - buffer is now free");
            async_tx_ready = true;
            break;

        case HAL_UART_EVENT_RX_DATA:
            // Not used in this test
            break;

        default:
            break;
    }
}

bool serv_uart_test_init(void)
{
    LOG_I(TAG, "Initializing UART test service");

    // Get default configuration
    hal_uart_config_t config = hal_uart_get_default_config();

    // Customize if needed (115200 8N1 is already default)
    config.baud_rate = 115200;
    config.rx_buffer_size = 256;  // Small buffer since we're only testing TX

    // Initialize UART
    // Note: HAL_UART_PORT_1 maps to USART2
    if (!hal_uart_init(HAL_UART_PORT_1, &config)) {
        LOG_E(TAG, "Failed to initialize UART");
        return false;
    }

    // Register callback for async tests
    if (!hal_uart_register_callback(HAL_UART_PORT_1, uart_test_event_callback, NULL)) {
        LOG_E(TAG, "Failed to register UART callback");
        return false;
    }

    LOG_I(TAG, "UART test service initialized");
    LOG_I(TAG, "Port: HAL_UART_PORT_1 -> USART2");
    LOG_I(TAG, "Baud Rate: 115200");
    LOG_I(TAG, "Format: 8N1 (8 data bits, No parity, 1 stop bit)");
    LOG_I(TAG, "TX Pin: Check your pinout (typically PA2 for USART2)");

    return true;
}

void serv_uart_test_send_pattern_blocking(void)
{
    LOG_I(TAG, "Starting UART test pattern (blocking)");

    // Test 1: ASCII text pattern (easy to see in protocol decoder)
    const char *test_msg = "UART_TEST_";
    LOG_I(TAG, "Sending ASCII pattern: %s (5 times)", test_msg);
    for (int i = 0; i < 5; i++) {
        hal_uart_write(HAL_UART_PORT_1, (uint8_t*)test_msg, strlen(test_msg), 1000);
        os_delay_ms(10);  // Small delay between repetitions
    }

    // Send newline for separation
    hal_uart_write(HAL_UART_PORT_1, (uint8_t*)"\r\n", 2, 1000);

    // Test 2: Bit patterns (for baud rate verification)
    LOG_I(TAG, "Sending bit patterns: 0x55, 0xAA");
    uint8_t bit_pattern[] = {0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA};
    hal_uart_write(HAL_UART_PORT_1, bit_pattern, sizeof(bit_pattern), 1000);
    os_delay_ms(10);

    // Test 3: Min/Max values
    LOG_I(TAG, "Sending min/max: 0x00, 0xFF");
    uint8_t minmax[] = {0x00, 0xFF, 0x00, 0xFF};
    hal_uart_write(HAL_UART_PORT_1, minmax, sizeof(minmax), 1000);
    os_delay_ms(10);

    // Test 4: Counting sequence (0x00 to 0xFF)
    LOG_I(TAG, "Sending counting sequence: 0x00 to 0xFF");
    uint8_t counting[256];
    for (int i = 0; i < 256; i++) {
        counting[i] = (uint8_t)i;
    }
    hal_uart_write(HAL_UART_PORT_1, counting, sizeof(counting), 1000);

    LOG_I(TAG, "UART test pattern complete!");
}

void serv_uart_test_send_pattern_async(void)
{
    if (!async_tx_ready) {
        LOG_W(TAG, "Previous async TX still in progress");
        return;
    }

    LOG_I(TAG, "Starting UART test pattern (async DMA)");

    // Build test pattern in buffer
    size_t offset = 0;

    // Test 1: ASCII text pattern
    const char *test_msg = "UART_TEST_ASYNC_";
    for (int i = 0; i < 3; i++) {
        size_t len = strlen(test_msg);
        memcpy(&async_tx_buffer[offset], test_msg, len);
        offset += len;
    }

    // Newline
    async_tx_buffer[offset++] = '\r';
    async_tx_buffer[offset++] = '\n';

    // Test 2: Bit patterns
    uint8_t bit_pattern[] = {0x55, 0xAA, 0x55, 0xAA};
    memcpy(&async_tx_buffer[offset], bit_pattern, sizeof(bit_pattern));
    offset += sizeof(bit_pattern);

    // Test 3: Counting sequence (first 100 bytes to fit in buffer)
    for (int i = 0; i < 100; i++) {
        async_tx_buffer[offset++] = (uint8_t)i;
    }

    // Start async transmission
    if (hal_uart_write_async(HAL_UART_PORT_1, async_tx_buffer, offset)) {
        async_tx_ready = false;
        LOG_I(TAG, "Async TX started (%d bytes) - waiting for completion", offset);
    } else {
        LOG_E(TAG, "Failed to start async TX");
    }
}

void serv_uart_test_loop(void)
{
    static uint32_t last_test_time = 0;
    uint32_t current_time = os_get_time_ms();

    // Send pattern every 2 seconds
    if (current_time - last_test_time >= 2000) {
        serv_uart_test_send_pattern_async();
        last_test_time = current_time;
    }
}
