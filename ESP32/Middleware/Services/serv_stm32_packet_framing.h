/**
 * @file serv_stm32_packet_framing.h
 * @brief STM32 Packet Framing Service
 *
 * Provides packet-based communication service with the STM32 microcontroller:
 * - Packet framing with start/end markers (0xAA/0x55)
 * - CRC16 validation
 * - Hardware flow control (RTS/CTS)
 * - Event-driven packet reception
 * - Timeout handling
 */

#ifndef SERV_STM32_PACKET_FRAMING_H
#define SERV_STM32_PACKET_FRAMING_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// ============================================================================
// Module-Specific Error Codes
// ============================================================================

/**
 * @brief STM32 UART Driver status codes
 *
 * Module-specific error codes for decoupled error handling.
 * Negative values indicate errors, zero indicates success.
 */
typedef enum {
    STM32_FRAMING_OK = 0,                        /**< Success */
    STM32_FRAMING_ERR_NOT_INITIALIZED = -1,      /**< Driver not initialized */
    STM32_FRAMING_ERR_ALREADY_INIT = -2,         /**< Already initialized */
    STM32_FRAMING_ERR_INVALID_PARAM = -3,        /**< Invalid parameter */
    STM32_FRAMING_ERR_TIMEOUT = -4,              /**< Transmission timeout */
    STM32_FRAMING_ERR_TX_FAILED = -5,            /**< Transmission failed */
    STM32_FRAMING_ERR_PACKET_TOO_LARGE = -6,     /**< Packet exceeds maximum size */
    STM32_FRAMING_ERR_CRC_FAILED = -7,           /**< CRC validation failed */
    STM32_FRAMING_ERR_FRAMING = -8,              /**< Framing error */
    STM32_FRAMING_ERR_BUFFER_OVERFLOW = -9,      /**< Buffer overflow */
    STM32_FRAMING_ERR_MEMORY = -10,              /**< Memory allocation failed */
} stm32_framing_status_t;

// ============================================================================
// Configuration Constants
// ============================================================================

#define STM32_UART_BAUD_RATE        921600
#define STM32_UART_RX_BUFFER_SIZE   2048
#define STM32_UART_TX_BUFFER_SIZE   1024
#define STM32_UART_MAX_PACKET_SIZE  512

// Packet framing markers
#define STM32_PACKET_START_MARKER   0xAA
#define STM32_PACKET_END_MARKER     0x55

// ============================================================================
// Data Types
// ============================================================================

/**
 * @brief UART driver event types
 */
typedef enum {
    STM32_FRAMING_EVENT_PACKET_RECEIVED,   /**< Complete packet received */
    STM32_FRAMING_EVENT_TX_COMPLETE,       /**< Transmission completed */
    STM32_FRAMING_EVENT_RX_ERROR,          /**< Reception error (framing, overflow) */
    STM32_FRAMING_EVENT_CRC_ERROR,         /**< CRC validation failed */
    STM32_FRAMING_EVENT_TIMEOUT,           /**< Reception timeout */
} stm32_framing_event_type_t;

/**
 * @brief UART driver event data
 */
typedef struct {
    stm32_framing_event_type_t type;       /**< Event type */
    uint8_t *data;                      /**< Pointer to packet data (for PACKET_RECEIVED) */
    size_t length;                      /**< Data length */
} stm32_framing_event_t;

/**
 * @brief Packet callback function type
 *
 * Called when a complete packet is received or an event occurs.
 *
 * @param event Pointer to event data
 * @param user_data User-provided context
 */
typedef void (*stm32_framing_callback_t)(stm32_framing_event_t *event, void *user_data);

/**
 * @brief Driver configuration structure
 */
typedef struct {
    uint32_t baud_rate;                 /**< Baud rate (default: 921600) */
    bool use_flow_control;              /**< Enable RTS/CTS flow control */
    uint32_t rx_timeout_ms;             /**< RX timeout in ms (0 = no timeout) */
    stm32_framing_callback_t callback;     /**< Event callback function */
    void *user_data;                    /**< User context for callback */
} stm32_framing_config_t;

/**
 * @brief Driver statistics
 */
typedef struct {
    uint32_t packets_sent;              /**< Total packets transmitted */
    uint32_t packets_received;          /**< Total packets received successfully */
    uint32_t crc_errors;                /**< CRC validation failures */
    uint32_t framing_errors;            /**< Framing errors (bad start/end markers) */
    uint32_t overflow_errors;           /**< Buffer overflow errors */
    uint32_t timeout_errors;            /**< Reception timeouts */
} stm32_framing_stats_t;

// ============================================================================
// Public API
// ============================================================================

/**
 * @brief Get default driver configuration
 * @return Default configuration structure
 */
stm32_framing_config_t stm32_framing_get_default_config(void);

/**
 * @brief Initialize the STM32 UART driver
 *
 * Sets up UART hardware, starts receive task, and enables packet processing.
 *
 * @param config Driver configuration
 * @return STM32_FRAMING_OK on success, error code otherwise
 */
stm32_framing_status_t stm32_framing_init(const stm32_framing_config_t *config);

/**
 * @brief Deinitialize the STM32 UART driver
 *
 * Stops receive task and releases UART resources.
 *
 * @return STM32_FRAMING_OK on success, error code otherwise
 */
stm32_framing_status_t stm32_framing_deinit(void);

/**
 * @brief Send a packet to STM32
 *
 * Frames the data with start/end markers and appends CRC16.
 * This function is thread-safe.
 *
 * @param data Pointer to data to send
 * @param length Data length (excluding framing and CRC)
 * @param timeout_ms Send timeout in milliseconds
 * @return STM32_FRAMING_OK on success, error code otherwise
 */
stm32_framing_status_t stm32_framing_send_packet(const uint8_t *data, size_t length, uint32_t timeout_ms);

/**
 * @brief Send raw bytes without framing
 *
 * Sends raw data without packet framing. Use with caution.
 *
 * @param data Pointer to data to send
 * @param length Data length
 * @param timeout_ms Send timeout in milliseconds
 * @return Number of bytes sent, or -1 on error
 */
int stm32_framing_send_raw(const uint8_t *data, size_t length, uint32_t timeout_ms);

/**
 * @brief Check if driver is initialized
 * @return true if initialized and ready
 */
bool stm32_framing_is_initialized(void);

/**
 * @brief Get driver statistics
 * @param stats Pointer to statistics structure to fill
 * @return STM32_FRAMING_OK on success
 */
stm32_framing_status_t stm32_framing_get_stats(stm32_framing_stats_t *stats);

/**
 * @brief Reset driver statistics
 */
void stm32_framing_reset_stats(void);

/**
 * @brief Flush receive buffer
 *
 * Discards any pending received data.
 *
 * @return STM32_FRAMING_OK on success
 */
stm32_framing_status_t stm32_framing_flush_rx(void);

// Note: CRC16 function is now provided by common/include/crc16.h
// Use crc16_ccitt() instead of the old stm32_framing_crc16()

#endif // SERV_STM32_PACKET_FRAMING_H
