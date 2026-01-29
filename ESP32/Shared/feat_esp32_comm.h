/**
 * @file feat_esp32_comm.h
 * @brief STM32 Feature: ESP32 Communication Handler
 *
 * Receives commands from ESP32 and sends responses.
 * This is the STM32-side counterpart to feat_stm32_protocol on ESP32.
 *
 * Copy to STM32 project: Middleware/Features/
 *
 * Dependencies:
 *   - protocol_common.h (shared definitions)
 *   - hal_uart.h (STM32 implementation)
 *   - os_wrapper.h (STM32 FreeRTOS implementation)
 */

#ifndef FEAT_ESP32_COMM_H
#define FEAT_ESP32_COMM_H

#include "protocol_common.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// ============================================================================
// Configuration
// ============================================================================

#define ESP32_COMM_TASK_STACK    2048
#define ESP32_COMM_TASK_PRIORITY 10
#define ESP32_COMM_RX_TIMEOUT_MS 100

// ============================================================================
// Callback Types
// ============================================================================

/**
 * @brief Command handler callback type
 *
 * Called when a command is received from ESP32. Handler should process
 * the command and return response data.
 *
 * @param cmd_id       Command ID
 * @param payload      Command payload (NULL if length is 0)
 * @param payload_len  Payload length
 * @param response     Buffer to write response payload
 * @param response_len Pointer to response length (set by handler)
 * @param max_response Maximum response buffer size
 * @return Response status code
 */
typedef response_status_t (*esp32_cmd_handler_t)(
    command_id_t cmd_id,
    const uint8_t *payload,
    size_t payload_len,
    uint8_t *response,
    size_t *response_len,
    size_t max_response
);

// ============================================================================
// Public API
// ============================================================================

/**
 * @brief Initialize ESP32 communication feature
 *
 * Sets up UART and prepares to receive commands.
 *
 * @return 0 on success, negative error code on failure
 */
int feat_esp32_comm_init(void);

/**
 * @brief Start ESP32 communication task
 *
 * Starts background task that listens for commands from ESP32.
 *
 * @return 0 on success, negative error code on failure
 */
int feat_esp32_comm_start(void);

/**
 * @brief Stop ESP32 communication
 */
void feat_esp32_comm_stop(void);

/**
 * @brief Register command handler
 *
 * Register callback to handle incoming commands from ESP32.
 *
 * @param handler Command handler function
 */
void feat_esp32_comm_register_handler(esp32_cmd_handler_t handler);

/**
 * @brief Send unsolicited notification to ESP32
 *
 * Use this to push data to ESP32 without being asked.
 *
 * @param cmd_id   Notification type (uses command ID space)
 * @param payload  Notification payload (can be NULL)
 * @param length   Payload length
 * @return 0 on success, negative error code on failure
 */
int feat_esp32_comm_send_notify(command_id_t cmd_id,
                                 const uint8_t *payload,
                                 size_t length);

/**
 * @brief Get communication statistics
 */
void feat_esp32_comm_get_stats(uint32_t *commands_received,
                                uint32_t *responses_sent,
                                uint32_t *errors);

#endif // FEAT_ESP32_COMM_H
