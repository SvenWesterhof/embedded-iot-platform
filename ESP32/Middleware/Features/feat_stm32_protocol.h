/**
 * @file feat_stm32_protocol.h
 * @brief STM32 Binary Protocol Feature (ESP32 Client Side)
 *
 * Implements high-level command/response protocol on top of the
 * STM32 packet framing service. Provides:
 * - Command packet building and sending
 * - Response parsing and handling
 * - Sequence number management
 * - Retry logic with exponential backoff
 * - Response callbacks
 *
 * Uses shared protocol definitions from protocol_common.h
 */

#ifndef FEAT_STM32_PROTOCOL_H
#define FEAT_STM32_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "protocol_common.h"

// ============================================================================
// Backward Compatibility Aliases
// ============================================================================

#define STM32_PROTOCOL_MAX_PAYLOAD_SIZE     PROTOCOL_MAX_PAYLOAD_SIZE
#define STM32_PROTOCOL_TIMEOUT_MS           PROTOCOL_TIMEOUT_MS
#define STM32_PROTOCOL_MAX_RETRIES          PROTOCOL_MAX_RETRIES
#define STM32_PROTOCOL_RETRY_BACKOFF_MS     PROTOCOL_RETRY_BACKOFF_MS

// ============================================================================
// Module-Specific Error Codes
// ============================================================================

/**
 * @brief STM32 Protocol Feature status codes
 * 
 * Module-specific error codes for decoupled error handling.
 * Negative values indicate errors, zero indicates success.
 */
typedef enum {
    FEAT_STM32_OK = 0,                      /**< Success */
    FEAT_STM32_ERR_NOT_INITIALIZED = -1,    /**< Feature not initialized */
    FEAT_STM32_ERR_ALREADY_INIT = -2,       /**< Already initialized */
    FEAT_STM32_ERR_INVALID_PARAM = -3,      /**< Invalid parameter */
    FEAT_STM32_ERR_TIMEOUT = -4,            /**< Command timeout */
    FEAT_STM32_ERR_NO_RESPONSE = -5,        /**< No response from STM32 */
    FEAT_STM32_ERR_BUSY = -6,               /**< Protocol busy */
    FEAT_STM32_ERR_COMM_FAILED = -7,        /**< Communication failure */
    FEAT_STM32_ERR_INVALID_STATE = -8,      /**< Invalid state for operation */
    FEAT_STM32_ERR_MEMORY = -9,             /**< Memory allocation failed */
    FEAT_STM32_ERR_QUEUE_FULL = -10,        /**< Command queue full */
} feat_stm32_status_t;

// ============================================================================
// Type Aliases (ESP32-specific naming to shared types)
// ============================================================================

/** Packet type alias for ESP32 code compatibility */
typedef packet_type_t stm32_packet_type_t;
#define STM32_PACKET_TYPE_CMD    PACKET_TYPE_CMD
#define STM32_PACKET_TYPE_RESP   PACKET_TYPE_RESP
#define STM32_PACKET_TYPE_NOTIFY PACKET_TYPE_NOTIFY

/** Command ID alias for ESP32 code compatibility */
typedef command_id_t stm32_command_id_t;

/** Response status alias for ESP32 code compatibility */
typedef response_status_t stm32_response_status_t;
#define RESP_STATUS_OK           RESP_OK
#define RESP_STATUS_ERROR        RESP_ERROR
#define RESP_STATUS_INVALID_CMD  RESP_INVALID_CMD
#define RESP_STATUS_INVALID_PARAM RESP_INVALID_PARAM
#define RESP_STATUS_BUSY         RESP_BUSY
#define RESP_STATUS_TIMEOUT      RESP_TIMEOUT
#define RESP_STATUS_NO_DATA      RESP_NO_DATA

/** Packet structure alias for ESP32 code compatibility */
typedef protocol_packet_t stm32_packet_t;

/**
 * @brief Response callback function type
 * 
 * Called when a response is received for a command.
 * 
 * @param cmd_id Command ID
 * @param seq Sequence number
 * @param status Response status
 * @param payload Payload data
 * @param length Payload length
 * @param user_data User context
 */
typedef void (*stm32_response_callback_t)(stm32_command_id_t cmd_id,
                                           uint8_t seq,
                                           stm32_response_status_t status,
                                           const uint8_t *payload,
                                           size_t length,
                                           void *user_data);

/**
 * @brief Notification callback function type
 * 
 * Called when an unsolicited notification is received from STM32.
 * 
 * @param cmd_id Notification type (uses command ID space)
 * @param payload Payload data
 * @param length Payload length
 * @param user_data User context
 */
typedef void (*stm32_notify_callback_t)(stm32_command_id_t cmd_id,
                                         const uint8_t *payload,
                                         size_t length,
                                         void *user_data);

// ============================================================================
// Public API
// ============================================================================

/**
 * @brief Initialize STM32 protocol feature
 *
 * Sets up UART driver and initializes protocol state.
 *
 * @return PROTO_OK on success, negative error code otherwise
 */
proto_err_t feat_stm32_protocol_init(void);

/**
 * @brief Start STM32 protocol feature
 *
 * Starts background tasks for handling responses and timeouts.
 *
 * @return PROTO_OK on success, negative error code otherwise
 */
proto_err_t feat_stm32_protocol_start(void);

/**
 * @brief Stop STM32 protocol feature
 */
void feat_stm32_protocol_stop(void);

/**
 * @brief Send a command to STM32
 * 
 * Sends command with automatic retry on failure. Blocks until
 * response received or timeout.
 * 
 * @param cmd Command ID
 * @param payload Command payload (can be NULL if length is 0)
 * @param length Payload length
 * @param response Buffer to store response payload (can be NULL)
 * @param response_len Pointer to response length (input: max size, output: actual size)
 * @param timeout_ms Timeout in milliseconds (0 = use default)
 * @return Response status code, or negative error code
 */
int stm32_protocol_send_command(stm32_command_id_t cmd,
                                 const void *payload,
                                 size_t length,
                                 void *response,
                                 size_t *response_len,
                                 uint32_t timeout_ms);

/**
 * @brief Send a command asynchronously
 *
 * Sends command and returns immediately. Response is delivered via callback.
 *
 * @param cmd Command ID
 * @param payload Command payload (can be NULL if length is 0)
 * @param length Payload length
 * @param callback Response callback function
 * @param user_data User context passed to callback
 * @return PROTO_OK on success, negative error code otherwise
 */
proto_err_t stm32_protocol_send_command_async(stm32_command_id_t cmd,
                                               const void *payload,
                                               size_t length,
                                               stm32_response_callback_t callback,
                                               void *user_data);

/**
 * @brief Register notification callback
 *
 * Register a callback for unsolicited notifications from STM32.
 *
 * @param callback Notification callback function
 * @param user_data User context passed to callback
 * @return PROTO_OK on success
 */
proto_err_t stm32_protocol_register_notify_callback(stm32_notify_callback_t callback,
                                                     void *user_data);

/**
 * @brief Check if protocol is initialized and ready
 * @return true if ready, false otherwise
 */
bool stm32_protocol_is_ready(void);

/**
 * @brief Get protocol statistics
 * 
 * Returns statistics about commands sent, responses received, etc.
 * 
 * @param commands_sent Pointer to store commands sent count
 * @param responses_received Pointer to store responses received count
 * @param timeouts Pointer to store timeout count
 * @param retries Pointer to store retry count
 */
void stm32_protocol_get_stats(uint32_t *commands_sent,
                               uint32_t *responses_received,
                               uint32_t *timeouts,
                               uint32_t *retries);

/**
 * @brief Reset protocol statistics
 */
void stm32_protocol_reset_stats(void);

// ============================================================================
// Helper Functions for Common Commands
// ============================================================================

/**
 * @brief Request historical buffer data from STM32
 *
 * @param start_index Starting index in buffer (0 = oldest)
 * @param count Number of records to retrieve
 * @param callback Response callback (async)
 * @param user_data User context
 * @return PROTO_OK on success
 */
proto_err_t stm32_cmd_get_buffer_data(uint32_t start_index,
                                       uint32_t count,
                                       stm32_response_callback_t callback,
                                       void *user_data);

/**
 * @brief Start live measurement on STM32
 *
 * @param interval_ms Measurement interval in milliseconds
 * @param callback Response callback (async)
 * @param user_data User context
 * @return PROTO_OK on success
 */
proto_err_t stm32_cmd_start_measurement(uint32_t interval_ms,
                                         stm32_response_callback_t callback,
                                         void *user_data);

/**
 * @brief Stop live measurement on STM32
 * @param callback Response callback (async)
 * @param user_data User context
 * @return PROTO_OK on success
 */
proto_err_t stm32_cmd_stop_measurement(stm32_response_callback_t callback,
                                        void *user_data);

/**
 * @brief Set STM32 RTC time
 *
 * @param unix_time Unix timestamp
 * @param callback Response callback (async)
 * @param user_data User context
 * @return PROTO_OK on success
 */
proto_err_t stm32_cmd_set_rtc(uint32_t unix_time,
                               stm32_response_callback_t callback,
                               void *user_data);

/**
 * @brief Get STM32 status
 *
 * @param callback Response callback (async)
 * @param user_data User context
 * @return PROTO_OK on success
 */
proto_err_t stm32_cmd_get_status(stm32_response_callback_t callback,
                                  void *user_data);

#endif // FEAT_STM32_PROTOCOL_H
