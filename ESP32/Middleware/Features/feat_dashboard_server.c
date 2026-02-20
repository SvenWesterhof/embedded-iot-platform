/**
 * @file feat_dashboard_server.c
 * @brief WebSocket Dashboard Server Implementation
 */

#include "feat_dashboard_server.h"
#include "feat_stm32_protocol.h"
#include "protocol_common.h"
#include "../../OS/event_bus.h"
#include "os_wrapper.h"
#include "portable_log.h"
#include "../Services/serv_ntp_sync.h"
#include <esp_http_server.h>
#include <string.h>

static const char *TAG = "DASHBOARD";

// ============================================================================
// Internal State
// ============================================================================

typedef struct {
    bool initialized;
    bool started;

    httpd_handle_t server;
    dashboard_client_t clients[DASHBOARD_MAX_CLIENTS];
    os_mutex_handle_t clients_mutex;

    dashboard_msg_callback_t msg_callback;
    dashboard_stats_t stats;

    os_task_handle_t heartbeat_task;

    // STM32 command forwarding state
    int stm32_requesting_client_fd;     // Client that sent the STM32 command
} dashboard_state_t;

static dashboard_state_t state = {0};

// ============================================================================
// Internal Functions
// ============================================================================

/**
 * @brief Find client by file descriptor
 */
static dashboard_client_t* find_client(int fd)
{
    for (int i = 0; i < DASHBOARD_MAX_CLIENTS; i++) {
        if (state.clients[i].active && state.clients[i].fd == fd) {
            return &state.clients[i];
        }
    }
    return NULL;
}

/**
 * @brief Add a new client
 */
static dashboard_client_t* add_client(int fd)
{
    os_mutex_take(state.clients_mutex, OS_WAIT_FOREVER);
    
    for (int i = 0; i < DASHBOARD_MAX_CLIENTS; i++) {
        if (!state.clients[i].active) {
            state.clients[i].fd = fd;
            state.clients[i].active = true;
            state.clients[i].last_heartbeat = os_get_time_ms();
            state.stats.clients_connected++;
            state.stats.total_connections++;
            
            os_mutex_give(state.clients_mutex);
            return &state.clients[i];
        }
    }
    
    os_mutex_give(state.clients_mutex);
    return NULL;
}

/**
 * @brief Remove a client
 */
static void remove_client(int fd)
{
    os_mutex_take(state.clients_mutex, OS_WAIT_FOREVER);
    
    for (int i = 0; i < DASHBOARD_MAX_CLIENTS; i++) {
        if (state.clients[i].active && state.clients[i].fd == fd) {
            state.clients[i].active = false;
            state.clients[i].fd = -1;
            if (state.stats.clients_connected > 0) {
                state.stats.clients_connected--;
            }
            break;
        }
    }
    
    os_mutex_give(state.clients_mutex);
    
    // Check if this was the last client
    if (!dashboard_has_active_clients()) {
        LOG_I(TAG, "Last client disconnected");
        event_bus_publish(EVENT_DASHBOARD_DISCONNECTED, NULL);
    }
}

/**
 * @brief STM32 async response callback - forwards response to dashboard client
 */
static void stm32_response_callback(stm32_command_id_t cmd_id,
                                     uint8_t seq,
                                     stm32_response_status_t status,
                                     const uint8_t *payload,
                                     size_t length,
                                     void *user_data)
{
    int client_fd = (int)(intptr_t)user_data;

    LOG_I(TAG, "STM32 response: cmd=0x%02X seq=%u status=0x%02X len=%u -> client %d",
          cmd_id, seq, status, length, client_fd);

    // Build response packet to send to dashboard
    // Format: DASH_RESP_STM32 + protocol_packet_t header + payload
    size_t resp_size = PROTOCOL_HEADER_SIZE + length;
    uint8_t *resp_buf = malloc(resp_size);
    if (resp_buf == NULL) {
        LOG_E(TAG, "Failed to allocate STM32 response buffer");
        return;
    }

    // Build protocol packet header
    protocol_packet_t *pkt = (protocol_packet_t *)resp_buf;
    pkt->type = PACKET_TYPE_RESP;
    pkt->cmd_id = (uint8_t)cmd_id;
    pkt->seq = seq;
    pkt->status = (uint8_t)status;
    pkt->length = length;

    if (payload && length > 0) {
        memcpy(pkt->payload, payload, length);
    }

    // Send to requesting client
    dashboard_send_to_client(client_fd, DASH_RESP_STM32, resp_buf, resp_size);

    free(resp_buf);
}

/**
 * @brief Forward STM32 command from dashboard
 * Dashboard sends only cmd_id, ESP32 decodes and builds payloads
 */
static void handle_stm32_command(int client_fd, const uint8_t *payload, size_t len)
{
    if (len < 1) {
        LOG_W(TAG, "STM32 command too short");
        uint8_t err = RESP_ERROR;
        dashboard_send_to_client(client_fd, DASH_RESP_ERROR, &err, 1);
        return;
    }

    if (!stm32_protocol_is_ready()) {
        LOG_W(TAG, "STM32 protocol not ready");
        uint8_t err = RESP_BUSY;
        dashboard_send_to_client(client_fd, DASH_RESP_ERROR, &err, 1);
        return;
    }

    // Payload format: cmd_id (1 byte) + optional parameters
    uint8_t cmd_id = payload[0];
    const uint8_t *params = (len > 1) ? &payload[1] : NULL;
    size_t params_len = (len > 1) ? len - 1 : 0;

    LOG_I(TAG, "Processing STM32 cmd=0x%02X from client %d", cmd_id, client_fd);

    proto_err_t err = PROTO_ERR_INVALID_ARG;

    // Decode command and call appropriate helper with response callback
    switch (cmd_id) {
        case CMD_SET_RTC: {
            // Get current time from NTP service
            time_t now = serv_ntp_get_time();
            if (now == 0) {
                LOG_W(TAG, "NTP time not available, RTC sync failed");
                err = PROTO_ERR_INVALID_STATE;
            } else {
                LOG_I(TAG, "Syncing RTC with NTP time: %lu", (unsigned long)now);
                err = stm32_cmd_set_rtc((uint32_t)now, stm32_response_callback, (void *)(intptr_t)client_fd);
            }
            break;
        }

        case CMD_GET_STATUS:
            err = stm32_cmd_get_status(stm32_response_callback, (void *)(intptr_t)client_fd);
            break;

        case CMD_START_MEASUREMENT: {
            // Extract parameters: sensor_type(1) + interval_ms(4) [optional, default to 1000ms]
            uint8_t sensor_type = (params_len >= 1) ? params[0] : SENSOR_TEMPERATURE;
            uint32_t interval_ms = 1000;  // Default
            
            if (params_len >= 5) {
                interval_ms = params[1] | (params[2] << 8) | 
                             (params[3] << 16) | (params[4] << 24);
            }
            
            LOG_I(TAG, "Start measurement: type=%u interval=%lu ms", sensor_type, (unsigned long)interval_ms);
            err = stm32_cmd_start_measurement(interval_ms, stm32_response_callback, (void *)(intptr_t)client_fd);
            break;
        }

        case CMD_STOP_MEASUREMENT:
            err = stm32_cmd_stop_measurement(stm32_response_callback, (void *)(intptr_t)client_fd);
            break;

        case CMD_GET_BUFFER_DATA: {
            // Extract parameters: start_index(4) + count(4) [optional, default to 0 and 10]
            uint32_t start_index = 0;
            uint32_t count = 10;
            
            if (params_len >= 4) {
                start_index = params[0] | (params[1] << 8) | 
                             (params[2] << 16) | (params[3] << 24);
            }
            if (params_len >= 8) {
                count = params[4] | (params[5] << 8) | 
                       (params[6] << 16) | (params[7] << 24);
            }
            
            LOG_I(TAG, "Get buffer data: start=%lu count=%lu", (unsigned long)start_index, (unsigned long)count);
            err = stm32_cmd_get_buffer_data(start_index, count, 
                                           stm32_response_callback, 
                                           (void *)(intptr_t)client_fd);
            break;
        }

        case CMD_CLEAR_BUFFER:
            // Use generic async command with callback
            err = stm32_protocol_send_command_async(
                CMD_CLEAR_BUFFER, NULL, 0,
                stm32_response_callback,
                (void *)(intptr_t)client_fd
            );
            break;

        case CMD_GET_CONFIG:
            err = stm32_protocol_send_command_async(
                CMD_GET_CONFIG, NULL, 0,
                stm32_response_callback,
                (void *)(intptr_t)client_fd
            );
            break;

        case CMD_SET_CONFIG:
            // Config payload should be in params
            err = stm32_protocol_send_command_async(
                CMD_SET_CONFIG, params, params_len,
                stm32_response_callback,
                (void *)(intptr_t)client_fd
            );
            break;

        default:
            LOG_W(TAG, "Unknown STM32 command: 0x%02X", cmd_id);
            err = PROTO_ERR_INVALID_ARG;
            break;
    }

    if (err != PROTO_OK) {
        LOG_E(TAG, "Failed to send STM32 command 0x%02X: %d", cmd_id, err);
        uint8_t resp_err = RESP_ERROR;
        dashboard_send_to_client(client_fd, DASH_RESP_ERROR, &resp_err, 1);
    }
}

/**
 * @brief Process received WebSocket message
 */
static void process_message(int client_fd, const uint8_t *data, size_t len)
{
    if (len < 1) {
        return;
    }
    
    state.stats.messages_received++;
    
    // First byte is message type
    dashboard_msg_type_t msg_type = (dashboard_msg_type_t)data[0];
    const uint8_t *payload = (len > 1) ? &data[1] : NULL;
    size_t payload_len = (len > 1) ? len - 1 : 0;
    
    LOG_D(TAG, "Received msg type=0x%02X, len=%u", msg_type, payload_len);
    
    // Update heartbeat on any message
    dashboard_client_t *client = find_client(client_fd);
    if (client) {
        client->last_heartbeat = os_get_time_ms();
    }
    
    // Handle message based on type
    switch (msg_type) {
        case DASH_MSG_HEARTBEAT:
            // Heartbeat already updated, just acknowledge
            LOG_D(TAG, "Heartbeat from client %d", client_fd);
            event_bus_publish(EVENT_DASHBOARD_HEARTBEAT, NULL);
            break;
            
        case DASH_MSG_REQUEST_HISTORY:
            LOG_I(TAG, "History request from client");
            event_bus_publish(EVENT_DASHBOARD_REQUEST_HISTORY, (void*)(intptr_t)client_fd);
            break;
            
        case DASH_MSG_START_MEASUREMENT:
            LOG_I(TAG, "Start measurement request");
            event_bus_publish(EVENT_DASHBOARD_START_MEASUREMENT, (void*)payload);
            break;
            
        case DASH_MSG_STOP_MEASUREMENT:
            LOG_I(TAG, "Stop measurement request");
            event_bus_publish(EVENT_DASHBOARD_STOP_MEASUREMENT, NULL);
            break;
            
        case DASH_MSG_GET_STATUS:
            LOG_D(TAG, "Status request from client");
            // Status request handled by application layer
            break;

        case DASH_MSG_STM32_CMD:
            handle_stm32_command(client_fd, payload, payload_len);
            break;

        default:
            LOG_W(TAG, "Unknown message type: 0x%02X", msg_type);
            break;
    }
    
    // Call user callback if registered
    if (state.msg_callback) {
        state.msg_callback(msg_type, payload, payload_len, client_fd);
    }
}

/**
 * @brief Index HTML page handler
 * Serves redirect message to external dashboard
 */
static esp_err_t index_html_handler(httpd_req_t *req)
{
    const char *html =
        "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
        "<title>ESP32 Gateway</title></head><body>"
        "<h1>ESP32 Gateway Dashboard</h1>"
        "<p>Please open <strong>tools/dashboard.html</strong> in your browser.</p>"
        "<p>The dashboard will connect to: <code>ws://" CONFIG_LWIP_LOCAL_HOSTNAME ".local/ws</code></p>"
        "<p>WebSocket endpoint is running on this device at <code>/ws</code></p>"
        "</body></html>";
    
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_sendstr(req, html);
}

/**
 * @brief WebSocket open handler
 */
static esp_err_t ws_open_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        LOG_I(TAG, "WebSocket handshake");
        return ESP_OK;
    }
    return ESP_OK;
}

/**
 * @brief WebSocket handler
 */
static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        // This is the handshake
        int fd = httpd_req_to_sockfd(req);
        dashboard_client_t *client = add_client(fd);
        
        if (client == NULL) {
            LOG_W(TAG, "Max clients reached, rejecting connection");
            return ESP_FAIL;
        }
        
        LOG_I(TAG, "New WebSocket client connected (fd=%d)", fd);
        
        // Notify if this is the first client
        if (state.stats.clients_connected == 1) {
            event_bus_publish(EVENT_DASHBOARD_CONNECTED, NULL);
        }
        
        return ESP_OK;
    }
    
    // Receive WebSocket frame
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_BINARY;
    
    // First call to get frame length
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        LOG_E(TAG, "httpd_ws_recv_frame failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    if (ws_pkt.len > 0 && ws_pkt.len <= DASHBOARD_MAX_MESSAGE_SIZE) {
        // Allocate buffer and receive payload
        uint8_t *buf = malloc(ws_pkt.len);
        if (buf == NULL) {
            LOG_E(TAG, "Failed to allocate buffer");
            return ESP_ERR_NO_MEM;
        }
        
        ws_pkt.payload = buf;
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        
        if (ret == ESP_OK) {
            int client_fd = httpd_req_to_sockfd(req);
            process_message(client_fd, buf, ws_pkt.len);
        }
        
        free(buf);
    }
    
    // Handle close frame
    if (ws_pkt.type == HTTPD_WS_TYPE_CLOSE) {
        int fd = httpd_req_to_sockfd(req);
        LOG_I(TAG, "WebSocket client disconnected (fd=%d)", fd);
        remove_client(fd);
    }
    
    return ESP_OK;
}

/**
 * @brief Heartbeat monitoring task
 */
static void heartbeat_task(void *arg)
{
    LOG_I(TAG, "Heartbeat monitor started");
    
    while (state.started) {
        uint32_t now = os_get_time_ms();
        
        os_mutex_take(state.clients_mutex, OS_WAIT_FOREVER);
        
        for (int i = 0; i < DASHBOARD_MAX_CLIENTS; i++) {
            if (state.clients[i].active) {
                uint32_t elapsed = now - state.clients[i].last_heartbeat;
                
                if (elapsed > DASHBOARD_HEARTBEAT_TIMEOUT_MS) {
                    LOG_W(TAG, "Client %d timed out (no heartbeat for %lu ms)",
                             state.clients[i].fd, (unsigned long)elapsed);
                    
                    int fd = state.clients[i].fd;
                    state.clients[i].active = false;
                    state.clients[i].fd = -1;
                    if (state.stats.clients_connected > 0) {
                        state.stats.clients_connected--;
                    }
                    
                    // Close the socket
                    httpd_sess_trigger_close(state.server, fd);
                }
            }
        }
        
        os_mutex_give(state.clients_mutex);
        
        // Check every 5 seconds
        os_delay_ms(5000);
    }
    
    LOG_I(TAG, "Heartbeat monitor stopped");
    os_task_delete(NULL);
}

// ============================================================================
// Public API Implementation
// ============================================================================

dashboard_status_t feat_dashboard_server_init(void)
{
    if (state.initialized) {
        LOG_W(TAG, "Already initialized");
        return DASHBOARD_ERR_ALREADY_INIT;
    }
    
    LOG_I(TAG, "Initializing dashboard server");
    
    // Create clients mutex
    state.clients_mutex = os_mutex_create();
    if (state.clients_mutex == NULL) {
        LOG_E(TAG, "Failed to create mutex");
        return DASHBOARD_ERR_MEMORY;
    }
    
    // Initialize client slots
    for (int i = 0; i < DASHBOARD_MAX_CLIENTS; i++) {
        state.clients[i].active = false;
        state.clients[i].fd = -1;
    }
    
    // Reset stats
    memset(&state.stats, 0, sizeof(dashboard_stats_t));
    
    state.initialized = true;
    LOG_I(TAG, "Dashboard server initialized");
    
    return DASHBOARD_OK;
}

dashboard_status_t feat_dashboard_server_start(void)
{
    if (!state.initialized) {
        LOG_E(TAG, "Not initialized");
        return DASHBOARD_ERR_NOT_INITIALIZED;
    }
    
    if (state.started) {
        LOG_W(TAG, "Already started");
        return DASHBOARD_OK;
    }
    
    LOG_I(TAG, "Starting dashboard server on port %d", DASHBOARD_WEBSOCKET_PORT);
    
    // Configure HTTP server
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = DASHBOARD_WEBSOCKET_PORT;
    config.ctrl_port = DASHBOARD_WEBSOCKET_PORT + 1;
    config.max_open_sockets = DASHBOARD_MAX_CLIENTS + 1;
    config.lru_purge_enable = true;
    
    esp_err_t err = httpd_start(&state.server, &config);
    if (err != ESP_OK) {
        LOG_E(TAG, "Failed to start HTTP server: %s", esp_err_to_name(err));
        return DASHBOARD_ERR_SERVER_START;
    }
    
    // Register index page handler
    httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_html_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(state.server, &index_uri);
    
    // Register WebSocket handler
    httpd_uri_t ws_uri = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = ws_handler,
        .user_ctx = NULL,
        .is_websocket = true,
        .handle_ws_control_frames = true,
    };
    
    err = httpd_register_uri_handler(state.server, &ws_uri);
    if (err != ESP_OK) {
        LOG_E(TAG, "Failed to register WebSocket handler: %s", esp_err_to_name(err));
        httpd_stop(state.server);
        return DASHBOARD_ERR_SERVER_START;
    }
    
    // Start heartbeat monitor task
    state.started = true;
    os_result_t ret = os_task_create_pinned(heartbeat_task, "dash_hb", 4096, NULL, 5, 
                                  &state.heartbeat_task, 1);
    if (ret != OS_SUCCESS) {
        LOG_W(TAG, "Failed to create heartbeat task");
        // Non-fatal, continue without heartbeat monitoring
    }
    
    LOG_I(TAG, "Dashboard server started");
    return DASHBOARD_OK;
}

void feat_dashboard_server_stop(void)
{
    if (!state.started) {
        return;
    }
    
    LOG_I(TAG, "Stopping dashboard server");
    
    state.started = false;
    
    // Wait for heartbeat task to exit
    os_delay_ms(100);
    
    // Close all clients
    dashboard_disconnect_all();
    
    // Stop HTTP server
    if (state.server) {
        httpd_stop(state.server);
        state.server = NULL;
    }
    
    LOG_I(TAG, "Dashboard server stopped");
}

bool dashboard_has_active_clients(void)
{
    os_mutex_take(state.clients_mutex, OS_WAIT_FOREVER);
    bool has_clients = (state.stats.clients_connected > 0);
    os_mutex_give(state.clients_mutex);
    return has_clients;
}

uint8_t dashboard_get_client_count(void)
{
    return state.stats.clients_connected;
}

dashboard_status_t dashboard_broadcast(dashboard_resp_type_t type,
                                        const void *data,
                                        size_t length)
{
    if (!state.started) {
        return DASHBOARD_ERR_NOT_INITIALIZED;
    }
    
    // Build message: type (1 byte) + data
    size_t msg_len = 1 + length;
    uint8_t *msg = malloc(msg_len);
    if (msg == NULL) {
        return DASHBOARD_ERR_MEMORY;
    }
    
    msg[0] = (uint8_t)type;
    if (data && length > 0) {
        memcpy(&msg[1], data, length);
    }
    
    httpd_ws_frame_t ws_pkt = {
        .final = true,
        .fragmented = false,
        .type = HTTPD_WS_TYPE_BINARY,
        .payload = msg,
        .len = msg_len,
    };
    
    xSemaphoreTake(state.clients_mutex, portMAX_DELAY);
    
    for (int i = 0; i < DASHBOARD_MAX_CLIENTS; i++) {
        if (state.clients[i].active) {
            esp_err_t err = httpd_ws_send_frame_async(state.server, 
                                                       state.clients[i].fd, 
                                                       &ws_pkt);
            if (err == ESP_OK) {
                state.stats.messages_sent++;
            } else {
                LOG_W(TAG, "Failed to send to client %d: %s", 
                         state.clients[i].fd, esp_err_to_name(err));
                state.stats.errors++;
            }
        }
    }
    
    os_mutex_give(state.clients_mutex);
    free(msg);
    
    return DASHBOARD_OK;
}

dashboard_status_t dashboard_send_to_client(int client_fd,
                                             dashboard_resp_type_t type,
                                             const void *data,
                                             size_t length)
{
    if (!state.started) {
        return DASHBOARD_ERR_NOT_INITIALIZED;
    }
    
    // Build message: type (1 byte) + data
    size_t msg_len = 1 + length;
    uint8_t *msg = malloc(msg_len);
    if (msg == NULL) {
        return DASHBOARD_ERR_MEMORY;
    }
    
    msg[0] = (uint8_t)type;
    if (data && length > 0) {
        memcpy(&msg[1], data, length);
    }
    
    httpd_ws_frame_t ws_pkt = {
        .final = true,
        .fragmented = false,
        .type = HTTPD_WS_TYPE_BINARY,
        .payload = msg,
        .len = msg_len,
    };
    
    esp_err_t err = httpd_ws_send_frame_async(state.server, client_fd, &ws_pkt);
    
    if (err == ESP_OK) {
        state.stats.messages_sent++;
    } else {
        state.stats.errors++;
    }
    
    free(msg);
    return (err == ESP_OK) ? DASHBOARD_OK : DASHBOARD_ERR_SEND_FAILED;
}

dashboard_status_t dashboard_broadcast_json(const char *json)
{
    if (!state.started || json == NULL) {
        return DASHBOARD_ERR_INVALID_PARAM;
    }
    
    size_t len = strlen(json);
    
    httpd_ws_frame_t ws_pkt = {
        .final = true,
        .fragmented = false,
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t*)json,
        .len = len,
    };
    
    xSemaphoreTake(state.clients_mutex, portMAX_DELAY);
    
    for (int i = 0; i < DASHBOARD_MAX_CLIENTS; i++) {
        if (state.clients[i].active) {
            esp_err_t err = httpd_ws_send_frame_async(state.server,
                                                       state.clients[i].fd,
                                                       &ws_pkt);
            if (err == ESP_OK) {
                state.stats.messages_sent++;
            }
        }
    }
    
    xSemaphoreGive(state.clients_mutex);
    return DASHBOARD_OK;
}

dashboard_status_t dashboard_register_msg_callback(dashboard_msg_callback_t callback)
{
    state.msg_callback = callback;
    return DASHBOARD_OK;
}

dashboard_status_t dashboard_get_stats(dashboard_stats_t *stats)
{
    if (stats == NULL) {
        return DASHBOARD_ERR_INVALID_PARAM;
    }
    
    memcpy(stats, &state.stats, sizeof(dashboard_stats_t));
    return DASHBOARD_OK;
}

void dashboard_disconnect_client(int client_fd)
{
    if (state.server) {
        httpd_sess_trigger_close(state.server, client_fd);
    }
    remove_client(client_fd);
}

void dashboard_disconnect_all(void)
{
    xSemaphoreTake(state.clients_mutex, portMAX_DELAY);
    
    for (int i = 0; i < DASHBOARD_MAX_CLIENTS; i++) {
        if (state.clients[i].active && state.server) {
            httpd_sess_trigger_close(state.server, state.clients[i].fd);
            state.clients[i].active = false;
            state.clients[i].fd = -1;
        }
    }
    
    state.stats.clients_connected = 0;
    xSemaphoreGive(state.clients_mutex);
}
