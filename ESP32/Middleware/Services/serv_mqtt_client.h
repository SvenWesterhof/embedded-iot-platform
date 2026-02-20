/**
 * @file serv_mqtt_client.h
 * @brief MQTT Client Service for IoT cloud integration
 * 
 * Background service providing MQTT connectivity for publishing sensor data
 * to cloud brokers and receiving commands.
 */

#ifndef SERV_MQTT_CLIENT_H
#define SERV_MQTT_CLIENT_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ═══════════════════════════════════════════════════════════════════════════
 * Module-Specific Error Codes
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief MQTT Client status codes
 * 
 * Module-specific error codes for decoupled error handling.
 * Negative values indicate errors, zero indicates success.
 */
typedef enum {
    MQTT_OK = 0,                            /**< Success */
    MQTT_ERR_NOT_INITIALIZED = -1,          /**< Client not initialized */
    MQTT_ERR_ALREADY_INIT = -2,             /**< Already initialized */
    MQTT_ERR_INVALID_PARAM = -3,            /**< Invalid parameter */
    MQTT_ERR_NOT_CONNECTED = -4,            /**< Not connected to broker */
    MQTT_ERR_CONNECT_FAILED = -5,           /**< Connection failed */
    MQTT_ERR_PUBLISH_FAILED = -6,           /**< Publish operation failed */
    MQTT_ERR_SUBSCRIBE_FAILED = -7,         /**< Subscribe operation failed */
    MQTT_ERR_MEMORY = -8,                   /**< Memory allocation failed */
    MQTT_ERR_TIMEOUT = -9,                  /**< Operation timeout */
    MQTT_ERR_INVALID_STATE = -10,           /**< Invalid state for operation */
} mqtt_status_t;

/* ═══════════════════════════════════════════════════════════════════════════
 * Configuration
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief MQTT broker configuration
 */
typedef struct {
    const char *broker_uri;         /**< Broker URI (e.g., "mqtts://xxx-ats.iot.region.amazonaws.com:8883") */
    const char *device_id;          /**< Device identifier (e.g., "esp32_001") */
    const char *client_id;          /**< MQTT client ID (NULL for auto-generate) */
    const char *username;           /**< Username (NULL if not required) */
    const char *password;           /**< Password (NULL if not required) */
    uint16_t keepalive_sec;         /**< Keepalive interval in seconds */
    uint8_t qos;                    /**< Default QoS level (0, 1, or 2) */
    bool clean_session;             /**< Clean session flag */

    /* TLS — all three must be set together, or all NULL for plaintext */
    const char *tls_ca_cert;        /**< PEM CA certificate (NULL = no TLS) */
    const char *tls_client_cert;    /**< PEM client certificate (NULL = server-only TLS) */
    const char *tls_client_key;     /**< PEM client private key (NULL = server-only TLS) */
} mqtt_client_config_t;

/**
 * @brief Default configuration macro
 */
#define MQTT_CLIENT_CONFIG_DEFAULT() {                      \
    .broker_uri = "mqtt://broker.hivemq.com:1883",          \
    .device_id = "esp32_gateway",                           \
    .client_id = NULL,                                      \
    .username = NULL,                                       \
    .password = NULL,                                       \
    .keepalive_sec = 120,                                   \
    .qos = 1,                                               \
    .clean_session = true,                                  \
    .tls_ca_cert = NULL,                                    \
    .tls_client_cert = NULL,                                \
    .tls_client_key = NULL,                                 \
}

/**
 * @brief MQTT connection state
 */
typedef enum {
    MQTT_STATE_DISCONNECTED = 0,
    MQTT_STATE_CONNECTING,
    MQTT_STATE_CONNECTED,
    MQTT_STATE_ERROR
} mqtt_client_state_t;

/**
 * @brief MQTT event types for callbacks (prefixed to avoid ESP-IDF conflicts)
 */
typedef enum {
    FEAT_MQTT_EVT_CONNECTED,
    FEAT_MQTT_EVT_DISCONNECTED,
    FEAT_MQTT_EVT_DATA_RECEIVED,
    FEAT_MQTT_EVT_PUBLISHED,
    FEAT_MQTT_EVT_ERROR
} feat_mqtt_event_t;

/**
 * @brief Received message structure
 */
typedef struct {
    const char *topic;              /**< Topic string (null-terminated) */
    uint16_t topic_len;             /**< Topic length */
    const uint8_t *data;            /**< Payload data */
    uint16_t data_len;              /**< Payload length */
    uint8_t qos;                    /**< QoS of received message */
    bool retained;                  /**< Retained flag */
} mqtt_message_t;

/**
 * @brief Callback for MQTT events
 * @param event Event type
 * @param message Message data (only valid for FEAT_MQTT_EVT_DATA_RECEIVED)
 */
typedef void (*mqtt_event_callback_t)(feat_mqtt_event_t event, const mqtt_message_t *message);

/* ═══════════════════════════════════════════════════════════════════════════
 * Lifecycle Functions
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Initialize MQTT client
 * @param config Configuration (NULL for defaults)
 * @return MQTT_OK on success
 */
mqtt_status_t serv_mqtt_init(const mqtt_client_config_t *config);

/**
 * @brief Start MQTT client (connect to broker)
 * @return MQTT_OK on success
 */
mqtt_status_t serv_mqtt_start(void);

/**
 * @brief Stop MQTT client (disconnect)
 * @return MQTT_OK on success
 */
mqtt_status_t serv_mqtt_stop(void);

/**
 * @brief Deinitialize MQTT client
 * @return MQTT_OK on success
 */
mqtt_status_t serv_mqtt_deinit(void);

/* ═══════════════════════════════════════════════════════════════════════════
 * Publishing Functions
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Publish data to exact topic
 * @param topic Full topic path (e.g., "devices/esp32_001/telemetry/temp")
 * @param data Payload data
 * @param len Payload length
 * @param qos QoS level (0-2), -1 for default
 * @param retain Retain flag
 * @return Message ID on success, -1 on failure
 */
int serv_mqtt_publish(const char *topic, const void *data, size_t len,
                      int qos, bool retain);

/**
 * @brief Publish string message to exact topic
 * @param topic Full topic path
 * @param message Null-terminated string
 * @param qos QoS level, -1 for default
 * @param retain Retain flag
 * @return Message ID on success, -1 on failure
 */
int serv_mqtt_publish_string(const char *topic, const char *message,
                             int qos, bool retain);

/* ═══════════════════════════════════════════════════════════════════════════
 * Subscription Functions
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Subscribe to exact topic
 *
 * Examples:
 * - "gateway/ota/notify"  (global broadcast)
 * - "devices/esp32_001/commands/#"  (device-specific with wildcard)
 *
 * @param topic Full topic path (supports MQTT wildcards + and #)
 * @param qos QoS level (0-2), -1 for default
 * @return Message ID on success, -1 on failure
 */
int serv_mqtt_subscribe(const char *topic, int qos);

/**
 * @brief Unsubscribe from exact topic
 * @param topic Full topic path
 * @return Message ID on success, -1 on failure
 */
int serv_mqtt_unsubscribe(const char *topic);

/**
 * @brief Register event callback
 * @param callback Callback function
 */
void serv_mqtt_set_callback(mqtt_event_callback_t callback);

/* ═══════════════════════════════════════════════════════════════════════════
 * Status Functions
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Get current connection state
 * @return Current state
 */
mqtt_client_state_t serv_mqtt_get_state(void);

/**
 * @brief Check if connected to broker
 * @return true if connected
 */
bool serv_mqtt_is_connected(void);

/**
 * @brief Get connection statistics
 * @param messages_sent Output: total messages sent
 * @param messages_received Output: total messages received
 * @param reconnect_count Output: number of reconnections
 */
void serv_mqtt_get_stats(uint32_t *messages_sent, uint32_t *messages_received,
                         uint32_t *reconnect_count);

/**
 * @brief Get configured device ID
 * @return Device ID string (NULL if not configured)
 */
const char* serv_mqtt_get_device_id(void);

/* ═══════════════════════════════════════════════════════════════════════════
 * Topic Helper Macros (Industry-Standard Hierarchy)
 * ═══════════════════════════════════════════════════════════════════════════ */

// Global/Broadcast topics (one-to-many)
#define MQTT_TOPIC_GLOBAL_OTA_NOTIFY        "gateway/ota/notify"
#define MQTT_TOPIC_GLOBAL_CONFIG            "gateway/config/update"
#define MQTT_TOPIC_GLOBAL_TIME              "gateway/system/time"

// Device-specific topic builders (requires device_id at runtime)
// Use with snprintf: snprintf(topic, sizeof(topic), MQTT_TOPIC_DEVICE_TELEMETRY_FMT, device_id, "temp");
#define MQTT_TOPIC_DEVICE_TELEMETRY_FMT     "devices/%s/telemetry/%s"  // (device_id, sensor_name)
#define MQTT_TOPIC_DEVICE_STATUS_FMT        "devices/%s/status"        // (device_id)
#define MQTT_TOPIC_DEVICE_COMMANDS_FMT      "devices/%s/commands/%s"   // (device_id, command)
#define MQTT_TOPIC_DEVICE_OTA_STATUS_FMT    "devices/%s/ota/status"    // (device_id)
#define MQTT_TOPIC_DEVICE_OTA_PROGRESS_FMT  "devices/%s/ota/progress"  // (device_id)

#ifdef __cplusplus
}
#endif

#endif /* SERV_MQTT_CLIENT_H */
