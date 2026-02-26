/**
 * @file serv_mqtt_client.c
 * @brief MQTT Client Service Implementation
 */

#include "serv_mqtt_client.h"
#include "mqtt_client.h"
#include "portable_log.h"
#include "os_wrapper.h"
#include "event_bus.h"
#include <string.h>
#include <stdio.h>
#include <time.h>

static const char *TAG = "MQTT_SVC";

/* ═══════════════════════════════════════════════════════════════════════════
 * Private Data
 * ═══════════════════════════════════════════════════════════════════════════ */

#define TOPIC_BUFFER_SIZE   128
#define JSON_BUFFER_SIZE    256

/**
 * @brief MQTT client context
 */
static struct {
    esp_mqtt_client_handle_t client;
    mqtt_client_config_t config;
    mqtt_client_state_t state;
    mqtt_event_callback_t user_callback;
    os_mutex_handle_t mutex;
    bool initialized;

    // Statistics
    uint32_t messages_sent;
    uint32_t messages_received;
    uint32_t reconnect_count;

    // Device ID buffer
    char device_id[64];
} ctx = {0};

/* ═══════════════════════════════════════════════════════════════════════════
 * Private Functions - Forward Declarations
 * ═══════════════════════════════════════════════════════════════════════════ */

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                                int32_t event_id, void *event_data);
static void handle_connected(void);
static void handle_disconnected(void);
static void handle_data(esp_mqtt_event_handle_t event);
static void handle_error(esp_mqtt_event_handle_t event);
static void event_bus_handler(event_type_t type, void *data);

/* ═══════════════════════════════════════════════════════════════════════════
 * Lifecycle Functions
 * ═══════════════════════════════════════════════════════════════════════════ */

mqtt_status_t serv_mqtt_init(const mqtt_client_config_t *config)
{
    if (ctx.initialized) {
        LOG_W(TAG, "Already initialized");
        return MQTT_ERR_ALREADY_INIT;
    }
    
    // Use default config if none provided
    if (config != NULL) {
        memcpy(&ctx.config, config, sizeof(mqtt_client_config_t));
    } else {
        mqtt_client_config_t default_config = MQTT_CLIENT_CONFIG_DEFAULT();
        memcpy(&ctx.config, &default_config, sizeof(mqtt_client_config_t));
    }

    // Store device ID
    if (ctx.config.device_id != NULL) {
        strncpy(ctx.device_id, ctx.config.device_id, sizeof(ctx.device_id) - 1);
        ctx.device_id[sizeof(ctx.device_id) - 1] = '\0';
    } else {
        strcpy(ctx.device_id, "esp32_gateway");
    }
    
    // Create mutex
    ctx.mutex = os_mutex_create();
    if (ctx.mutex == NULL) {
        LOG_E(TAG, "Failed to create mutex");
        return MQTT_ERR_MEMORY;
    }
    
    // Configure MQTT client
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker = {
            .address.uri = ctx.config.broker_uri,
        },
        .credentials = {
            .client_id = ctx.config.client_id,
            .username = ctx.config.username,
            .authentication = {
                .password = ctx.config.password,
            },
        },
        .session = {
            .keepalive = ctx.config.keepalive_sec,
            .disable_clean_session = !ctx.config.clean_session,
        },
        .network = {
            .reconnect_timeout_ms = 10000,
        },
        .buffer = {
            // 4096 bytes: OTA notification payloads include pre-signed S3 URLs
            // which can exceed 500 bytes alone. 1024 was too small and caused
            // fragmented receive + malformed PUBACK → CLIENT_ERROR on AWS IoT Core.
            .size = 4096,
        },
    };
    
    // Apply TLS if CA cert provided
    // NOTE: ESP-IDF treats *_len == 0 as a file path, not an in-memory buffer.
    //       Always set the length (strlen + 1 includes the null terminator
    //       required by mbedTLS's PEM parser).
    if (ctx.config.tls_ca_cert != NULL) {
        size_t ca_len   = strlen(ctx.config.tls_ca_cert) + 1;
        mqtt_cfg.broker.verification.certificate     = ctx.config.tls_ca_cert;
        mqtt_cfg.broker.verification.certificate_len = ca_len;
        LOG_I(TAG, "TLS: CA cert len=%d", (int)ca_len);
        if (ctx.config.tls_client_cert != NULL && ctx.config.tls_client_key != NULL) {
            size_t cert_len = strlen(ctx.config.tls_client_cert) + 1;
            size_t key_len  = strlen(ctx.config.tls_client_key) + 1;
            mqtt_cfg.credentials.authentication.certificate     = ctx.config.tls_client_cert;
            mqtt_cfg.credentials.authentication.certificate_len = cert_len;
            mqtt_cfg.credentials.authentication.key             = ctx.config.tls_client_key;
            mqtt_cfg.credentials.authentication.key_len         = key_len;
            LOG_I(TAG, "TLS: client cert len=%d, key len=%d, key prefix=%.27s",
                  (int)cert_len, (int)key_len, ctx.config.tls_client_key);
        }
        LOG_I(TAG, "TLS enabled (mTLS: %s)",
              ctx.config.tls_client_cert != NULL ? "yes" : "no");
    }

    // Create client
    ctx.client = esp_mqtt_client_init(&mqtt_cfg);
    if (ctx.client == NULL) {
        LOG_E(TAG, "Failed to create MQTT client");
        vSemaphoreDelete(ctx.mutex);
        ctx.mutex = NULL;
        return MQTT_ERR_CONNECT_FAILED;
    }
    
    // Register event handler
    esp_err_t ret = esp_mqtt_client_register_event(ctx.client, ESP_EVENT_ANY_ID,
                                                    mqtt_event_handler, NULL);
    if (ret != ESP_OK) {
        LOG_E(TAG, "Failed to register event handler");
        esp_mqtt_client_destroy(ctx.client);
        vSemaphoreDelete(ctx.mutex);
        return MQTT_ERR_CONNECT_FAILED;
    }
    
    // Subscribe to event bus for STM32 data
    event_bus_subscribe(EVENT_STM32_DATA_READY, event_bus_handler);
    
    ctx.state = MQTT_STATE_DISCONNECTED;
    ctx.initialized = true;
    
    LOG_I(TAG, "Initialized - broker: %s", ctx.config.broker_uri);
    return MQTT_OK;
}

mqtt_status_t serv_mqtt_start(void)
{
    if (!ctx.initialized) {
        return MQTT_ERR_NOT_INITIALIZED;
    }
    
    if (ctx.state == MQTT_STATE_CONNECTED || ctx.state == MQTT_STATE_CONNECTING) {
        LOG_W(TAG, "Already started");
        return MQTT_OK;
    }
    
    ctx.state = MQTT_STATE_CONNECTING;
    esp_err_t ret = esp_mqtt_client_start(ctx.client);
    
    if (ret != ESP_OK) {
        LOG_E(TAG, "Failed to start client: %s", esp_err_to_name(ret));
        ctx.state = MQTT_STATE_ERROR;
        return MQTT_ERR_CONNECT_FAILED;
    }
    
    LOG_I(TAG, "Connecting to broker...");
    return MQTT_OK;
}

mqtt_status_t serv_mqtt_stop(void)
{
    if (!ctx.initialized) {
        return MQTT_ERR_NOT_INITIALIZED;
    }
    
    esp_err_t ret = esp_mqtt_client_stop(ctx.client);
    ctx.state = MQTT_STATE_DISCONNECTED;
    
    LOG_I(TAG, "Stopped");
    return (ret == ESP_OK) ? MQTT_OK : MQTT_ERR_INVALID_STATE;
}

mqtt_status_t serv_mqtt_deinit(void)
{
    if (!ctx.initialized) {
        return MQTT_ERR_NOT_INITIALIZED;
    }
    
    // Unsubscribe from event bus
    event_bus_unsubscribe(EVENT_STM32_DATA_READY, event_bus_handler);
    
    // Stop and destroy client
    esp_mqtt_client_stop(ctx.client);
    esp_mqtt_client_destroy(ctx.client);
    
    if (ctx.mutex != NULL) {
        vSemaphoreDelete(ctx.mutex);
        ctx.mutex = NULL;
    }
    
    memset(&ctx, 0, sizeof(ctx));
    
    LOG_I(TAG, "Deinitialized");
    return MQTT_OK;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Publishing Functions
 * ═══════════════════════════════════════════════════════════════════════════ */

int serv_mqtt_publish(const char *topic, const void *data, size_t len,
                      int qos, bool retain)
{
    if (!ctx.initialized || ctx.state != MQTT_STATE_CONNECTED) {
        return -1;
    }

    if (topic == NULL || data == NULL) {
        return -1;
    }

    // Use default QoS if not specified
    if (qos < 0) {
        qos = ctx.config.qos;
    }

    int msg_id = esp_mqtt_client_publish(ctx.client, topic,
                                          (const char *)data, len,
                                          qos, retain ? 1 : 0);

    if (msg_id >= 0) {
        xSemaphoreTake(ctx.mutex, portMAX_DELAY);
        ctx.messages_sent++;
        xSemaphoreGive(ctx.mutex);

        LOG_D(TAG, "Published to %s (msg_id=%d)", topic, msg_id);
    }

    return msg_id;
}

int serv_mqtt_publish_string(const char *topic, const char *message,
                             int qos, bool retain)
{
    if (message == NULL) {
        return -1;
    }
    return serv_mqtt_publish(topic, message, strlen(message), qos, retain);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Subscription Functions
 * ═══════════════════════════════════════════════════════════════════════════ */

int serv_mqtt_subscribe(const char *topic, int qos)
{
    if (!ctx.initialized || ctx.state != MQTT_STATE_CONNECTED) {
        return -1;
    }

    if (topic == NULL) {
        return -1;
    }

    if (qos < 0) {
        qos = ctx.config.qos;
    }

    int msg_id = esp_mqtt_client_subscribe(ctx.client, topic, qos);

    if (msg_id >= 0) {
        LOG_I(TAG, "Subscribed to %s (msg_id=%d)", topic, msg_id);
    }

    return msg_id;
}

int serv_mqtt_unsubscribe(const char *topic)
{
    if (!ctx.initialized || ctx.state != MQTT_STATE_CONNECTED) {
        return -1;
    }

    if (topic == NULL) {
        return -1;
    }

    return esp_mqtt_client_unsubscribe(ctx.client, topic);
}

void serv_mqtt_set_callback(mqtt_event_callback_t callback)
{
    ctx.user_callback = callback;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Status Functions
 * ═══════════════════════════════════════════════════════════════════════════ */

mqtt_client_state_t serv_mqtt_get_state(void)
{
    return ctx.state;
}

bool serv_mqtt_is_connected(void)
{
    return ctx.state == MQTT_STATE_CONNECTED;
}

void serv_mqtt_get_stats(uint32_t *messages_sent, uint32_t *messages_received,
                         uint32_t *reconnect_count)
{
    xSemaphoreTake(ctx.mutex, portMAX_DELAY);

    if (messages_sent) *messages_sent = ctx.messages_sent;
    if (messages_received) *messages_received = ctx.messages_received;
    if (reconnect_count) *reconnect_count = ctx.reconnect_count;

    xSemaphoreGive(ctx.mutex);
}

const char* serv_mqtt_get_device_id(void)
{
    return ctx.initialized ? ctx.device_id : NULL;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Private Functions - Event Handling
 * ═══════════════════════════════════════════════════════════════════════════ */

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                                int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            handle_connected();
            break;
            
        case MQTT_EVENT_DISCONNECTED:
            handle_disconnected();
            break;
            
        case MQTT_EVENT_DATA:
            handle_data(event);
            break;
            
        case MQTT_EVENT_ERROR:
            handle_error(event);
            break;
            
        case MQTT_EVENT_PUBLISHED:
            LOG_D(TAG, "Message published (msg_id=%d)", event->msg_id);
            if (ctx.user_callback) {
                ctx.user_callback(FEAT_MQTT_EVT_PUBLISHED, NULL);
            }
            break;
            
        case MQTT_EVENT_SUBSCRIBED:
            LOG_D(TAG, "Subscribed (msg_id=%d)", event->msg_id);
            break;
            
        case MQTT_EVENT_UNSUBSCRIBED:
            LOG_D(TAG, "Unsubscribed (msg_id=%d)", event->msg_id);
            break;
            
        default:
            LOG_D(TAG, "Unhandled event: %ld", event_id);
            break;
    }
}

static void handle_connected(void)
{
    LOG_I(TAG, "Connected to broker (device_id: %s)", ctx.device_id);

    mqtt_client_state_t prev_state = ctx.state;
    ctx.state = MQTT_STATE_CONNECTED;

    // Count reconnections
    if (prev_state != MQTT_STATE_CONNECTING) {
        xSemaphoreTake(ctx.mutex, portMAX_DELAY);
        ctx.reconnect_count++;
        xSemaphoreGive(ctx.mutex);
    }

    // Publish to event bus — subscribers run in the event bus task, which is the
    // correct context to call serv_mqtt_publish from. Do NOT publish MQTT messages
    // here: this callback runs inside the MQTT client task, and calling
    // esp_mqtt_client_publish mid-CONNACK transition sends the packet before the
    // MQTT state machine re-enters its main loop, producing a CLIENT_ERROR on
    // AWS IoT Core. Move any on-connect publishes to the EVENT_MQTT_CONNECTED handler.
    event_bus_publish(EVENT_MQTT_CONNECTED, NULL);

    // User callback
    if (ctx.user_callback) {
        ctx.user_callback(FEAT_MQTT_EVT_CONNECTED, NULL);
    }
}

static void handle_disconnected(void)
{
    LOG_W(TAG, "Disconnected from broker");
    ctx.state = MQTT_STATE_DISCONNECTED;
    
    // Publish to event bus
    event_bus_publish(EVENT_MQTT_DISCONNECTED, NULL);
    
    // User callback
    if (ctx.user_callback) {
        ctx.user_callback(FEAT_MQTT_EVT_DISCONNECTED, NULL);
    }
}

static void handle_data(esp_mqtt_event_handle_t event)
{
    LOG_I(TAG, "Received: topic=%.*s, data=%.*s",
             event->topic_len, event->topic,
             event->data_len, event->data);
    
    xSemaphoreTake(ctx.mutex, portMAX_DELAY);
    ctx.messages_received++;
    xSemaphoreGive(ctx.mutex);
    
    // Build message structure for callback
    mqtt_message_t message = {
        .topic = event->topic,
        .topic_len = event->topic_len,
        .data = (const uint8_t *)event->data,
        .data_len = event->data_len,
        .qos = event->qos,
        .retained = event->retain
    };
    
    // User callback
    if (ctx.user_callback) {
        ctx.user_callback(FEAT_MQTT_EVT_DATA_RECEIVED, &message);
    }

    // Publish to event bus for other components (copy data — MQTT frees it after this handler)
    event_bus_publish_copy(EVENT_MQTT_DATA_RECEIVED, event->data, event->data_len);
}

static void handle_error(esp_mqtt_event_handle_t event)
{
    LOG_E(TAG, "MQTT error occurred");
    
    if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
        LOG_E(TAG, "Transport error: %s", 
                 strerror(event->error_handle->esp_transport_sock_errno));
    }
    
    ctx.state = MQTT_STATE_ERROR;
    
    if (ctx.user_callback) {
        ctx.user_callback(MQTT_EVENT_ERROR, NULL);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Private Functions - Utilities
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Event bus handler for STM32 data
 *
 * Automatically forwards STM32 data to MQTT broker using device-specific topic
 */
static void event_bus_handler(event_type_t type, void *data)
{
    if (!ctx.initialized || ctx.state != MQTT_STATE_CONNECTED) {
        return;
    }

    if (type == EVENT_STM32_DATA_READY && data != NULL) {
        // Forward raw data to MQTT using device-specific telemetry topic
        // In real implementation, you'd parse the STM32 data structure here
        char topic[TOPIC_BUFFER_SIZE];
        (void)snprintf(topic, sizeof(topic), "devices/%s/telemetry/stm32", ctx.device_id);
        serv_mqtt_publish_string(topic, (const char *)data, 0, false);

        LOG_D(TAG, "Forwarded STM32 data to MQTT");
    }
}
