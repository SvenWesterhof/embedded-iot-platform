/**
 * @file protocol_common.h
 * @brief Shared protocol definitions between ESP32 and STM32
 *
 * This file should be identical on both sides.
 * Copy to both projects: Shared/ or a common include path.
 */

#ifndef PROTOCOL_COMMON_H
#define PROTOCOL_COMMON_H

#include <stdint.h>

// ============================================================================
// Portable Error Codes
// ============================================================================

typedef enum {
    PROTO_OK              =  0,   /**< Success */
    PROTO_ERR_INVALID_ARG = -1,   /**< Invalid argument */
    PROTO_ERR_NO_MEM      = -2,   /**< Out of memory */
    PROTO_ERR_INVALID_STATE = -3, /**< Invalid state */
    PROTO_ERR_TIMEOUT     = -4,   /**< Operation timeout */
    PROTO_ERR_INVALID_SIZE = -5,  /**< Invalid size */
    PROTO_ERR_NOT_FOUND   = -6,   /**< Resource not found */
    PROTO_ERR_FAIL        = -7,   /**< General failure */
} proto_err_t;

// ============================================================================
// Protocol Constants
// ============================================================================

#define PROTOCOL_MAX_PAYLOAD_SIZE     256
#define PROTOCOL_TIMEOUT_MS           5000
#define PROTOCOL_MAX_RETRIES          3
#define PROTOCOL_RETRY_BACKOFF_MS     100

// UART framing markers (used by lower layer)
#define PACKET_START_MARKER           0xAA
#define PACKET_END_MARKER             0x55

// ============================================================================
// Packet Types
// ============================================================================

typedef enum {
    PACKET_TYPE_CMD    = 0x01,    /**< Command from ESP32 to STM32 */
    PACKET_TYPE_RESP   = 0x02,    /**< Response from STM32 to ESP32 */
    PACKET_TYPE_NOTIFY = 0x03,    /**< Unsolicited notification from STM32 */
} packet_type_t;

// ============================================================================
// Command IDs
// ============================================================================

typedef enum {
    CMD_GET_BUFFER_DATA    = 0x01,   /**< Request historical data from buffer */
    CMD_START_MEASUREMENT  = 0x02,   /**< Start live measurement */
    CMD_STOP_MEASUREMENT   = 0x03,   /**< Stop measurement */
    CMD_SET_RTC            = 0x04,   /**< Set STM32 RTC time */
    CMD_GET_STATUS         = 0x05,   /**< Get STM32 status */
    CMD_CLEAR_BUFFER       = 0x06,   /**< Clear data buffer */
    CMD_GET_CONFIG         = 0x07,   /**< Get configuration */
    CMD_SET_CONFIG         = 0x08,   /**< Set configuration */

    // Notification IDs (0x80+)
    NOTIFY_SENSOR_DATA     = 0x80,   /**< Live sensor data notification */
} command_id_t;

// ============================================================================
// Response Status Codes
// ============================================================================

typedef enum {
    RESP_OK            = 0x00,   /**< Command successful */
    RESP_ERROR         = 0x01,   /**< General error */
    RESP_INVALID_CMD   = 0x02,   /**< Invalid command */
    RESP_INVALID_PARAM = 0x03,   /**< Invalid parameter */
    RESP_BUSY          = 0x04,   /**< Device busy */
    RESP_TIMEOUT       = 0x05,   /**< Operation timeout */
    RESP_NO_DATA       = 0x06,   /**< No data available */
} response_status_t;

// ============================================================================
// Packet Structure
// ============================================================================

/**
 * Wire format: TYPE(1) + CMD_ID(1) + SEQ(1) + STATUS(1) + LENGTH(2) + PAYLOAD(0-256)
 * Total header size: 6 bytes
 */
typedef struct {
    uint8_t type;                              /**< Packet type (CMD/RESP/NOTIFY) */
    uint8_t cmd_id;                            /**< Command identifier */
    uint8_t seq;                               /**< Sequence number */
    uint8_t status;                            /**< Response status (RESP only) */
    uint16_t length;                           /**< Payload length */
    uint8_t payload[PROTOCOL_MAX_PAYLOAD_SIZE]; /**< Payload data */
} __attribute__((packed)) protocol_packet_t;

#define PROTOCOL_HEADER_SIZE  6

// ============================================================================
// Common Payload Structures
// ============================================================================

/** GET_BUFFER_DATA request payload */
typedef struct {
    uint32_t start_index;
    uint32_t count;
} __attribute__((packed)) cmd_get_buffer_data_t;

/** START_MEASUREMENT request payload */
typedef struct {
    uint32_t interval_ms;
} __attribute__((packed)) cmd_start_measurement_t;

/** SET_RTC request payload */
typedef struct {
    uint32_t unix_time;
} __attribute__((packed)) cmd_set_rtc_t;

/** GET_STATUS response payload */
typedef struct {
    uint8_t  state;           /**< Current device state */
    uint8_t  error_code;      /**< Last error code */
    uint16_t buffer_count;    /**< Records in buffer */
    uint32_t uptime_sec;      /**< Uptime in seconds */
} __attribute__((packed)) resp_get_status_t;

// ============================================================================
// Sensor Types
// ============================================================================

typedef enum {
    SENSOR_TEMPERATURE = 0x01,
    SENSOR_CURRENT     = 0x02,
    // Future sensors can be added here
} sensor_type_t;

// ============================================================================
// Sensor Data Structures
// ============================================================================

/** Single sensor sample (used for buffered data and streaming) */
typedef struct {
    uint8_t  sensor_type;     /**< Sensor type (sensor_type_t) */
    uint32_t timestamp;       /**< Unix timestamp or ms since boot */
    int32_t  value;           /**< Scaled value (e.g., temp_C * 100, current_mA) */
} __attribute__((packed)) sensor_sample_t;

/** GET_BUFFER_DATA response - array of samples follows header */
typedef struct {
    uint8_t  sensor_type;     /**< Sensor type requested */
    uint16_t sample_count;    /**< Number of samples in payload */
    // Followed by: sensor_sample_t samples[sample_count]
} __attribute__((packed)) resp_buffer_data_header_t;

/** START_MEASUREMENT extended - specify which sensor */
typedef struct {
    uint8_t  sensor_type;     /**< Which sensor to stream */
    uint32_t interval_ms;     /**< Sample interval in ms */
} __attribute__((packed)) cmd_start_stream_t;

#endif // PROTOCOL_COMMON_H
