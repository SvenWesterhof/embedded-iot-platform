/**
 * @file feat_dashboard_server.h
 * @brief WebSocket Dashboard Server Feature
 * 
 * Provides WebSocket server for real-time dashboard communication:
 * - Client connection management
 * - Dashboard presence detection (heartbeat)
 * - Parse dashboard commands
 * - Forward measurement data to connected clients
 * - Publish dashboard events to event bus
 */

#ifndef FEAT_DASHBOARD_SERVER_H
#define FEAT_DASHBOARD_SERVER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <esp_err.h>

// ============================================================================
// Configuration Constants
// ============================================================================

#define DASHBOARD_WEBSOCKET_PORT        80
#define DASHBOARD_MAX_CLIENTS           4
#define DASHBOARD_HEARTBEAT_TIMEOUT_MS  60000
#define DASHBOARD_MAX_MESSAGE_SIZE      4096

// ============================================================================
// Module-Specific Error Codes
// ============================================================================

/**
 * @brief Dashboard Server status codes
 * 
 * Module-specific error codes for decoupled error handling.
 * Negative values indicate errors, zero indicates success.
 */
typedef enum {
    DASHBOARD_OK = 0,                       /**< Success */
    DASHBOARD_ERR_NOT_INITIALIZED = -1,     /**< Server not initialized */
    DASHBOARD_ERR_ALREADY_INIT = -2,        /**< Already initialized */
    DASHBOARD_ERR_INVALID_PARAM = -3,       /**< Invalid parameter */
    DASHBOARD_ERR_NO_CLIENTS = -4,          /**< No clients connected */
    DASHBOARD_ERR_CLIENT_NOT_FOUND = -5,    /**< Client not found */
    DASHBOARD_ERR_SEND_FAILED = -6,         /**< Send operation failed */
    DASHBOARD_ERR_MEMORY = -7,              /**< Memory allocation failed */
    DASHBOARD_ERR_SERVER_START = -8,        /**< Server start failed */
    DASHBOARD_ERR_INVALID_MESSAGE = -9,     /**< Invalid message format */
} dashboard_status_t;

// ============================================================================
// Data Types
// ============================================================================

/**
 * @brief Dashboard message types (from dashboard to ESP32)
 */
typedef enum {
    DASH_MSG_HEARTBEAT = 0x01,          /**< Keep-alive heartbeat */
    DASH_MSG_REQUEST_HISTORY = 0x02,    /**< Request historical data */
    DASH_MSG_START_MEASUREMENT = 0x03,  /**< Start live measurement */
    DASH_MSG_STOP_MEASUREMENT = 0x04,   /**< Stop measurement */
    DASH_MSG_GET_STATUS = 0x05,         /**< Request system status */
    DASH_MSG_SET_CONFIG = 0x06,         /**< Set configuration */
    DASH_MSG_SYNC_TIME = 0x07,          /**< Request time sync */

    // STM32 command forwarding
    DASH_MSG_STM32_CMD = 0x10,          /**< Forward raw STM32 command (payload: cmd_id + data) */
} dashboard_msg_type_t;

/**
 * @brief Dashboard response types (from ESP32 to dashboard)
 */
typedef enum {
    DASH_RESP_ACK = 0x01,               /**< Command acknowledged */
    DASH_RESP_ERROR = 0x02,             /**< Error occurred */
    DASH_RESP_STATUS = 0x03,            /**< Status response */
    DASH_RESP_DATA = 0x04,              /**< Data packet */
    DASH_RESP_MEASUREMENT = 0x05,       /**< Live measurement data */
    DASH_RESP_HISTORY = 0x06,           /**< Historical data chunk */

    // STM32 response forwarding
    DASH_RESP_STM32 = 0x10,             /**< Raw STM32 response (full protocol_packet_t) */
} dashboard_resp_type_t;

/**
 * @brief Dashboard client info
 */
typedef struct {
    int fd;                             /**< Socket file descriptor */
    uint32_t last_heartbeat;            /**< Last heartbeat timestamp (ms) */
    bool active;                        /**< Client is active */
    char ip_addr[16];                   /**< Client IP address */
} dashboard_client_t;

/**
 * @brief Dashboard server statistics
 */
typedef struct {
    uint32_t clients_connected;         /**< Currently connected clients */
    uint32_t total_connections;         /**< Total connections since start */
    uint32_t messages_received;         /**< Messages from dashboard */
    uint32_t messages_sent;             /**< Messages to dashboard */
    uint32_t errors;                    /**< Error count */
} dashboard_stats_t;

/**
 * @brief Dashboard message callback type
 * 
 * Called when a message is received from dashboard.
 * 
 * @param msg_type Message type
 * @param payload Message payload
 * @param length Payload length
 * @param client_fd Client file descriptor (for direct response)
 */
typedef void (*dashboard_msg_callback_t)(dashboard_msg_type_t msg_type,
                                          const uint8_t *payload,
                                          size_t length,
                                          int client_fd);

// ============================================================================
// Public API
// ============================================================================

/**
 * @brief Initialize dashboard server
 * 
 * Sets up HTTP server with WebSocket support.
 * 
 * @return DASHBOARD_OK on success, error code otherwise
 */
dashboard_status_t feat_dashboard_server_init(void);

/**
 * @brief Start dashboard server
 * 
 * Starts listening for WebSocket connections.
 * 
 * @return DASHBOARD_OK on success, error code otherwise
 */
dashboard_status_t feat_dashboard_server_start(void);

/**
 * @brief Stop dashboard server
 */
void feat_dashboard_server_stop(void);

/**
 * @brief Check if any dashboard clients are connected
 * 
 * @return true if at least one active client, false otherwise
 */
bool dashboard_has_active_clients(void);

/**
 * @brief Get number of connected clients
 * 
 * @return Number of active clients
 */
uint8_t dashboard_get_client_count(void);

/**
 * @brief Send data to all connected dashboard clients
 * 
 * @param type Response type
 * @param data Data to send
 * @param length Data length
 * @return DASHBOARD_OK on success, error code otherwise
 */
dashboard_status_t dashboard_broadcast(dashboard_resp_type_t type,
                                        const void *data,
                                        size_t length);

/**
 * @brief Send data to a specific client
 * 
 * @param client_fd Client file descriptor
 * @param type Response type
 * @param data Data to send
 * @param length Data length
 * @return DASHBOARD_OK on success, error code otherwise
 */
dashboard_status_t dashboard_send_to_client(int client_fd,
                                             dashboard_resp_type_t type,
                                             const void *data,
                                             size_t length);

/**
 * @brief Send JSON string to all clients
 * 
 * @param json NULL-terminated JSON string
 * @return DASHBOARD_OK on success
 */
dashboard_status_t dashboard_broadcast_json(const char *json);

/**
 * @brief Register message callback
 * 
 * Register a callback to receive dashboard messages.
 * 
 * @param callback Callback function
 * @return DASHBOARD_OK on success
 */
dashboard_status_t dashboard_register_msg_callback(dashboard_msg_callback_t callback);

/**
 * @brief Get dashboard server statistics
 * 
 * @param stats Pointer to statistics structure
 * @return DASHBOARD_OK on success
 */
dashboard_status_t dashboard_get_stats(dashboard_stats_t *stats);

/**
 * @brief Disconnect a specific client
 * 
 * @param client_fd Client file descriptor
 */
void dashboard_disconnect_client(int client_fd);

/**
 * @brief Disconnect all clients
 */
void dashboard_disconnect_all(void);

#endif // FEAT_DASHBOARD_SERVER_H
