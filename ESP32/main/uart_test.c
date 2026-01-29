/**
 * @file uart_test.c
 * @brief STM32 Protocol layer test
 *
 * Tests feat_stm32_protocol with:
 * - Protocol packet structure (type, cmd_id, seq, status, length, payload)
 * - Command/response handling
 * - Async callbacks
 *
 * Note: Without real STM32, commands will timeout.
 * For loopback: Won't work directly as protocol expects specific response format.
 */

#include <stdio.h>
#include <string.h>
#include "feat_stm32_protocol.h"
#include "portable_log.h"
#include "os_wrapper.h"
#include "esp_err.h"

static const char *TAG = "PROTO_TEST";

// Response callback for async commands
static void response_callback(stm32_command_id_t cmd_id,
                               uint8_t seq,
                               stm32_response_status_t status,
                               const uint8_t *payload,
                               size_t length,
                               void *user_data)
{
    LOG_I(TAG, "Response: cmd=0x%02X, seq=%u, status=%u, len=%u",
          cmd_id, seq, status, length);

    if (status == RESP_STATUS_OK && length > 0) {
        LOG_I(TAG, "  Payload: %.*s", (int)length, payload);
    }
}

// Notification callback for unsolicited messages
static void notify_callback(stm32_command_id_t cmd_id,
                             const uint8_t *payload,
                             size_t length,
                             void *user_data)
{
    LOG_I(TAG, "Notification: cmd=0x%02X, len=%u", cmd_id, length);
    if (length > 0) {
        LOG_I(TAG, "  Payload: %.*s", (int)length, payload);
    }
}

void uart_test_run(void)
{
    LOG_I(TAG, "=== STM32 Protocol Test ===");
    LOG_I(TAG, "Protocol packet: [TYPE][CMD_ID][SEQ][STATUS][LEN_L][LEN_H][PAYLOAD]");
    LOG_I(TAG, "Note: Commands will timeout without real STM32");

    // Initialize protocol layer
    esp_err_t err = feat_stm32_protocol_init();
    if (err != ESP_OK) {
        LOG_E(TAG, "Protocol init failed: %d", err);
        return;
    }

    // Register notification callback
    stm32_protocol_register_notify_callback(notify_callback, NULL);

    // Start protocol task (handles timeouts/retries)
    err = feat_stm32_protocol_start();
    if (err != ESP_OK) {
        LOG_E(TAG, "Protocol start failed: %d", err);
        return;
    }

    LOG_I(TAG, "Protocol ready. Sending test commands...");
    LOG_I(TAG, "Available commands:");
    LOG_I(TAG, "  0x01: GET_BUFFER_DATA");
    LOG_I(TAG, "  0x02: START_MEASUREMENT");
    LOG_I(TAG, "  0x03: STOP_MEASUREMENT");
    LOG_I(TAG, "  0x04: SET_RTC");
    LOG_I(TAG, "  0x05: GET_STATUS");

    int cycle = 0;

    while (1) {
        cycle++;

        // Test different commands
        switch (cycle % 4) {
            case 0:
                LOG_I(TAG, "[%d] Sending GET_STATUS (async)", cycle);
                err = stm32_cmd_get_status(response_callback, NULL);
                if (err != ESP_OK) {
                    LOG_E(TAG, "Send failed: %d", err);
                }
                break;

            case 1:
                LOG_I(TAG, "[%d] Sending STOP_MEASUREMENT (sync)", cycle);
                int status = stm32_protocol_send_command(CMD_STOP_MEASUREMENT,
                                                          NULL, 0, NULL, NULL, 2000);
                LOG_I(TAG, "  Result: %d", status);
                break;

            case 2:
                LOG_I(TAG, "[%d] Sending SET_RTC (sync)", cycle);
                uint32_t unix_time = 1700000000;  // Example timestamp
                status = stm32_protocol_send_command(CMD_SET_RTC,
                                                      &unix_time, sizeof(unix_time),
                                                      NULL, NULL, 2000);
                LOG_I(TAG, "  Result: %d", status);
                break;

            case 3:
                LOG_I(TAG, "[%d] Sending GET_BUFFER_DATA (async)", cycle);
                err = stm32_cmd_get_buffer_data(0, 10, response_callback, NULL);
                if (err != ESP_OK) {
                    LOG_E(TAG, "Send failed: %d", err);
                }
                break;
        }

        // Print stats
        uint32_t cmd_sent, resp_recv, timeouts, retries;
        stm32_protocol_get_stats(&cmd_sent, &resp_recv, &timeouts, &retries);
        LOG_I(TAG, "Stats: sent=%lu, recv=%lu, timeout=%lu, retry=%lu",
              cmd_sent, resp_recv, timeouts, retries);

        os_delay_ms(3000);
    }
}
