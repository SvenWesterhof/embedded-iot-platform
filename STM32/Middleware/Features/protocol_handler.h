/**
 * @file protocol_handler.h
 * @brief Protocol handler for ESP32-STM32 communication
 *
 * Sits on top of esp32_packet_framing layer.
 * Receives commands, dispatches to handlers, sends responses/notifications.
 */

#ifndef PROTOCOL_HANDLER_H
#define PROTOCOL_HANDLER_H

#include <stdint.h>
#include <stdbool.h>
#include "protocol_common.h"

// ============================================================================
// Error Codes
// ============================================================================

typedef enum {
    PROTO_HANDLER_OK = 0,
    PROTO_HANDLER_ERR_NOT_INIT = -1,
    PROTO_HANDLER_ERR_ALREADY_INIT = -2,
    PROTO_HANDLER_ERR_TX_FAILED = -3,
    PROTO_HANDLER_ERR_INVALID_PARAM = -4,
} proto_handler_status_t;

// ============================================================================
// Public API
// ============================================================================

/**
 * @brief Initialize protocol handler
 *
 * Initializes the packet framing layer and registers for incoming packets.
 *
 * @return PROTO_HANDLER_OK on success
 */
proto_handler_status_t protocol_handler_init(void);

/**
 * @brief Deinitialize protocol handler
 *
 * @return PROTO_HANDLER_OK on success
 */
proto_handler_status_t protocol_handler_deinit(void);

/**
 * @brief Check if protocol handler is initialized
 *
 * @return true if initialized
 */
bool protocol_handler_is_initialized(void);

/**
 * @brief Send a response packet
 *
 * @param cmd_id Command ID being responded to
 * @param seq Sequence number from original command
 * @param status Response status code
 * @param payload Response payload (can be NULL)
 * @param payload_len Payload length
 * @return PROTO_HANDLER_OK on success
 */
proto_handler_status_t protocol_handler_send_response(
    uint8_t cmd_id,
    uint8_t seq,
    response_status_t status,
    const void *payload,
    uint16_t payload_len);

/**
 * @brief Send a notification packet (unsolicited)
 *
 * Used for streaming sensor data.
 *
 * @param cmd_id Command ID context (e.g., CMD_START_MEASUREMENT)
 * @param payload Notification payload
 * @param payload_len Payload length
 * @return PROTO_HANDLER_OK on success
 */
proto_handler_status_t protocol_handler_send_notification(
    uint8_t cmd_id,
    const void *payload,
    uint16_t payload_len);

/**
 * @brief Send a sensor sample notification
 *
 * Convenience function for streaming sensor data.
 *
 * @param sample Sensor sample to send
 * @return PROTO_HANDLER_OK on success
 */
proto_handler_status_t protocol_handler_send_sensor_sample(
    const sensor_sample_t *sample);

/**
 * @brief Start streaming sensor data
 *
 * @param sensor_type Which sensor to stream
 * @param interval_ms Interval between samples in ms
 * @return PROTO_HANDLER_OK on success
 */
proto_handler_status_t protocol_handler_start_stream(
    sensor_type_t sensor_type,
    uint32_t interval_ms);

/**
 * @brief Stop streaming sensor data
 *
 * @return PROTO_HANDLER_OK on success
 */
proto_handler_status_t protocol_handler_stop_stream(void);

/**
 * @brief Check if streaming is active
 *
 * @return true if streaming
 */
bool protocol_handler_is_streaming(void);

#endif // PROTOCOL_HANDLER_H
