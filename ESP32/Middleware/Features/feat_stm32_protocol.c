/**
 * @file feat_stm32_protocol.c
 * @brief STM32 Binary Protocol Feature Implementation
 */

#include "feat_stm32_protocol.h"
#include "../Services/serv_stm32_packet_framing.h"
#include "../../OS/event_bus.h"
#include "os_wrapper.h"
#include "portable_log.h"
#include <string.h>

static const char *TAG = "STM32_PROTO";

// ============================================================================
// Internal Constants
// ============================================================================

#define PROTOCOL_TASK_STACK_SIZE    4096
#define PROTOCOL_TASK_PRIORITY      8
#define PENDING_CMD_QUEUE_SIZE      10
#define MAX_PENDING_COMMANDS        10

// ============================================================================
// Internal Data Structures
// ============================================================================

/**
 * @brief Pending command tracking
 */
typedef struct {
    bool active;
    uint8_t seq;
    stm32_command_id_t cmd_id;
    uint32_t send_time;
    uint32_t timeout_ms;
    uint8_t retries_left;

    // Stored payload for retries
    uint8_t payload[STM32_PROTOCOL_MAX_PAYLOAD_SIZE];
    size_t payload_len;

    // For synchronous calls
    bool waiting;
    os_semaphore_handle_t response_sem;
    stm32_response_status_t response_status;
    uint8_t *response_buffer;
    size_t *response_len;
    size_t response_max_len;

    // For asynchronous calls
    stm32_response_callback_t callback;
    void *user_data;
} pending_command_t;

/**
 * @brief Protocol state
 */
typedef struct {
    bool initialized;
    bool running;
    uint8_t next_seq;
    
    // Task handles
    os_task_handle_t protocol_task_handle;
    
    // Command tracking
    pending_command_t pending_cmds[MAX_PENDING_COMMANDS];
    os_mutex_handle_t pending_mutex;
    
    // Notification callback
    stm32_notify_callback_t notify_callback;
    void *notify_user_data;
    
    // Statistics
    uint32_t commands_sent;
    uint32_t responses_received;
    uint32_t timeouts;
    uint32_t retries;
} protocol_state_t;

static protocol_state_t state = {0};

// ============================================================================
// Internal Functions
// ============================================================================

/**
 * @brief Get next sequence number
 */
static uint8_t get_next_seq(void)
{
    uint8_t seq = state.next_seq++;
    if (state.next_seq == 0) {
        state.next_seq = 1;  // Skip 0
    }
    return seq;
}

/**
 * @brief Find pending command by sequence number
 */
static pending_command_t* find_pending_command(uint8_t seq)
{
    for (int i = 0; i < MAX_PENDING_COMMANDS; i++) {
        if (state.pending_cmds[i].active && state.pending_cmds[i].seq == seq) {
            return &state.pending_cmds[i];
        }
    }
    return NULL;
}

/**
 * @brief Allocate a pending command slot
 */
static pending_command_t* alloc_pending_command(void)
{
    for (int i = 0; i < MAX_PENDING_COMMANDS; i++) {
        if (!state.pending_cmds[i].active) {
            memset(&state.pending_cmds[i], 0, sizeof(pending_command_t));
            state.pending_cmds[i].active = true;
            return &state.pending_cmds[i];
        }
    }
    return NULL;
}

/**
 * @brief Free a pending command slot
 */
static void free_pending_command(pending_command_t *cmd)
{
    if (cmd->response_sem != NULL) {
        os_semaphore_delete(cmd->response_sem);
    }
    cmd->active = false;
}

/**
 * @brief Build and send a command packet
 */
static proto_err_t send_command_packet(stm32_command_id_t cmd_id,
                                        uint8_t seq,
                                        const void *payload,
                                        size_t length)
{
    if (length > STM32_PROTOCOL_MAX_PAYLOAD_SIZE) {
        LOG_E(TAG, "Payload too large: %zu bytes", length);
        return PROTO_ERR_INVALID_SIZE;
    }

    // Build packet
    stm32_packet_t packet;
    packet.type = STM32_PACKET_TYPE_CMD;
    packet.cmd_id = (uint8_t)cmd_id;
    packet.seq = seq;
    packet.status = 0;  // Not used in commands
    packet.length = length;

    if (payload != NULL && length > 0) {
        memcpy(packet.payload, payload, length);
    }

    // Calculate total size (header + payload)
    size_t packet_size = PROTOCOL_HEADER_SIZE + length;

    // Send via UART driver (convert stm32_framing_status_t to proto_err_t)
    stm32_framing_status_t uart_result = stm32_framing_send_packet((uint8_t*)&packet, packet_size,
                                                                STM32_PROTOCOL_TIMEOUT_MS);

    if (uart_result == STM32_FRAMING_OK) {
        state.commands_sent++;
        LOG_D(TAG, "Sent CMD 0x%02X (seq=%u, len=%zu)", cmd_id, seq, length);
        return PROTO_OK;
    }

    LOG_E(TAG, "Failed to send CMD 0x%02X (UART error: %d)", cmd_id, uart_result);
    return PROTO_ERR_FAIL;
}

/**
 * @brief Handle received packet from UART
 */
static void handle_received_packet(const uint8_t *data, size_t length)
{
    if (length < PROTOCOL_HEADER_SIZE) {
        LOG_W(TAG, "Packet too short: %zu bytes", length);
        return;
    }

    // Parse packet header
    stm32_packet_t *packet = (stm32_packet_t*)data;

    LOG_D(TAG, "Received packet: type=0x%02X, cmd=0x%02X, seq=%u, status=0x%02X, len=%u",
             packet->type, packet->cmd_id, packet->seq, packet->status, packet->length);

    // Verify length consistency
    if (PROTOCOL_HEADER_SIZE + packet->length != length) {
        LOG_W(TAG, "Length mismatch: header says %u, received %zu",
                 packet->length, length - PROTOCOL_HEADER_SIZE);
        return;
    }
    
    os_mutex_take(state.pending_mutex, OS_WAIT_FOREVER);
    
    switch (packet->type) {
        case STM32_PACKET_TYPE_RESP: {
            // Find matching pending command
            pending_command_t *pending = find_pending_command(packet->seq);
            
            if (pending == NULL) {
                LOG_W(TAG, "Response for unknown seq=%u", packet->seq);
                os_mutex_give(state.pending_mutex);
                return;
            }
            
            state.responses_received++;
            
            // Handle synchronous command
            if (pending->waiting) {
                pending->response_status = (stm32_response_status_t)packet->status;
                
                // Copy response payload if buffer provided
                if (pending->response_buffer != NULL && pending->response_len != NULL) {
                    size_t copy_len = (packet->length < pending->response_max_len) ?
                                      packet->length : pending->response_max_len;
                    memcpy(pending->response_buffer, packet->payload, copy_len);
                    *pending->response_len = copy_len;
                }
                
                // Signal waiting task
                os_semaphore_give(pending->response_sem);
            }
            // Handle asynchronous command
            else if (pending->callback != NULL) {
                pending->callback((stm32_command_id_t)packet->cmd_id,
                                 packet->seq,
                                 (stm32_response_status_t)packet->status,
                                 packet->payload,
                                 packet->length,
                                 pending->user_data);
                
                // Free pending command (async callbacks are one-shot)
                free_pending_command(pending);
            }
            
            // Publish high-level event
            if (packet->status == RESP_STATUS_OK) {
                event_bus_publish(EVENT_STM32_ACK_RECEIVED, NULL);
            } else {
                event_bus_publish(EVENT_STM32_ERROR, (void*)(uintptr_t)packet->status);
            }
            
            break;
        }
        
        case STM32_PACKET_TYPE_NOTIFY: {
            // Unsolicited notification from STM32
            LOG_I(TAG, "Notification: cmd=0x%02X, len=%u", packet->cmd_id, packet->length);
            
            if (state.notify_callback != NULL) {
                state.notify_callback((stm32_command_id_t)packet->cmd_id,
                                     packet->payload,
                                     packet->length,
                                     state.notify_user_data);
            }
            
            // Publish event (copy data — rx_buffer is overwritten by next packet)
            event_bus_publish_copy(EVENT_STM32_DATA_READY, data, length);
            break;
        }
        
        default:
            LOG_W(TAG, "Unknown packet type: 0x%02X", packet->type);
            break;
    }
    
    os_mutex_give(state.pending_mutex);
}

/**
 * @brief UART event callback
 */
static void uart_event_callback(stm32_framing_event_t *event, void *user_data)
{
    switch (event->type) {
        case STM32_FRAMING_EVENT_PACKET_RECEIVED:
            handle_received_packet(event->data, event->length);
            break;

        case STM32_FRAMING_EVENT_CRC_ERROR:
            LOG_W(TAG, "UART CRC error");
            // cppcheck-suppress intToPointerCast
            event_bus_publish(EVENT_STM32_ERROR, (void*)1);
            break;

        case STM32_FRAMING_EVENT_RX_ERROR:
            LOG_W(TAG, "UART RX error");
            // cppcheck-suppress intToPointerCast
            event_bus_publish(EVENT_STM32_ERROR, (void*)2);
            break;

        case STM32_FRAMING_EVENT_TIMEOUT:
            LOG_W(TAG, "UART timeout");
            event_bus_publish(EVENT_STM32_TIMEOUT, NULL);
            break;

        default:
            break;
    }
}

/**
 * @brief Protocol maintenance task
 * 
 * Handles timeouts and retries for pending commands.
 */
static void protocol_task(void *arg)
{
    LOG_I(TAG, "Protocol task started");
    
    while (state.running) {
        uint32_t now = os_get_time_ms();
        
        os_mutex_take(state.pending_mutex, OS_WAIT_FOREVER);
        
        // Check for timeouts
        for (int i = 0; i < MAX_PENDING_COMMANDS; i++) {
            pending_command_t *cmd = &state.pending_cmds[i];
            
            if (!cmd->active) {
                continue;
            }
            
            // Check if command has timed out
            if ((now - cmd->send_time) > cmd->timeout_ms) {
                if (cmd->retries_left > 0) {
                    // Retry command — resend the stored payload
                    LOG_W(TAG, "Retrying CMD 0x%02X (seq=%u), retries left=%u",
                             cmd->cmd_id, cmd->seq, cmd->retries_left);

                    cmd->retries_left--;
                    cmd->send_time = now;
                    state.retries++;

                    proto_err_t retry_err = send_command_packet(
                        cmd->cmd_id, cmd->seq, cmd->payload, cmd->payload_len);
                    if (retry_err != PROTO_OK) {
                        LOG_E(TAG, "Retry send failed for CMD 0x%02X", cmd->cmd_id);
                    }

                } else {
                    // Max retries exceeded
                    LOG_E(TAG, "CMD 0x%02X (seq=%u) timeout after %u retries",
                             cmd->cmd_id, cmd->seq, STM32_PROTOCOL_MAX_RETRIES);
                    
                    state.timeouts++;
                    
                    // Signal synchronous caller with timeout
                    if (cmd->waiting && cmd->response_sem != NULL) {
                        cmd->response_status = RESP_STATUS_TIMEOUT;
                        os_semaphore_give(cmd->response_sem);
                    }
                    // Call async callback with error
                    else if (cmd->callback != NULL) {
                        cmd->callback(cmd->cmd_id, cmd->seq, RESP_STATUS_TIMEOUT,
                                     NULL, 0, cmd->user_data);
                    }
                    
                    event_bus_publish(EVENT_STM32_TIMEOUT, NULL);
                    
                    // Free the command slot
                    free_pending_command(cmd);
                }
            }
        }
        
        os_mutex_give(state.pending_mutex);
        
        // Sleep for 100ms
        os_delay_ms(100);
    }
    
    LOG_I(TAG, "Protocol task stopped");
    os_task_delete(NULL);
}

// ============================================================================
// Public API Implementation
// ============================================================================

proto_err_t feat_stm32_protocol_init(void)
{
    if (state.initialized) {
        LOG_W(TAG, "Already initialized");
        return PROTO_ERR_INVALID_STATE;
    }

    LOG_I(TAG, "Initializing STM32 protocol feature");

    // Create pending command mutex
    state.pending_mutex = os_mutex_create();
    if (state.pending_mutex == NULL) {
        LOG_E(TAG, "Failed to create pending mutex");
        return PROTO_ERR_NO_MEM;
    }

    // Initialize UART driver
    stm32_framing_config_t uart_config = stm32_framing_get_default_config();

    uart_config.callback = uart_event_callback;
    stm32_framing_status_t uart_result = stm32_framing_init(&uart_config);
    if (uart_result != STM32_FRAMING_OK) {
        LOG_E(TAG, "Failed to initialize UART driver (error: %d)", uart_result);
        os_mutex_delete(state.pending_mutex);
        return PROTO_ERR_FAIL;
    }

    // Initialize state
    state.next_seq = 1;
    state.notify_callback = NULL;
    state.notify_user_data = NULL;
    memset(&state.pending_cmds, 0, sizeof(state.pending_cmds));
    state.commands_sent = 0;
    state.responses_received = 0;
    state.timeouts = 0;
    state.retries = 0;

    state.initialized = true;
    LOG_I(TAG, "STM32 protocol feature initialized");

    return PROTO_OK;
}

proto_err_t feat_stm32_protocol_start(void)
{
    if (!state.initialized) {
        LOG_E(TAG, "Not initialized");
        return PROTO_ERR_INVALID_STATE;
    }

    if (state.running) {
        LOG_W(TAG, "Already running");
        return PROTO_OK;
    }

    LOG_I(TAG, "Starting STM32 protocol feature");

    // Create protocol task
    state.running = true;
    os_result_t ret = os_task_create_pinned(protocol_task, "stm32_proto",
                                  PROTOCOL_TASK_STACK_SIZE, NULL,
                                  PROTOCOL_TASK_PRIORITY, &state.protocol_task_handle, 1);

    if (ret != OS_SUCCESS) {
        LOG_E(TAG, "Failed to create protocol task");
        state.running = false;
        return PROTO_ERR_NO_MEM;
    }

    LOG_I(TAG, "STM32 protocol feature started");
    return PROTO_OK;
}

void feat_stm32_protocol_stop(void)
{
    if (!state.running) {
        return;
    }
    
    LOG_I(TAG, "Stopping STM32 protocol feature");
    
    state.running = false;
    
    // Wait for task to terminate
    if (state.protocol_task_handle != NULL) {
        os_delay_ms(200);  // Give task time to exit
        state.protocol_task_handle = NULL;
    }
    
    // Clean up pending commands
    os_mutex_take(state.pending_mutex, OS_WAIT_FOREVER);
    for (int i = 0; i < MAX_PENDING_COMMANDS; i++) {
        if (state.pending_cmds[i].active) {
            free_pending_command(&state.pending_cmds[i]);
        }
    }
    os_mutex_give(state.pending_mutex);
    
    LOG_I(TAG, "STM32 protocol feature stopped");
}

int stm32_protocol_send_command(stm32_command_id_t cmd,
                                 const void *payload,
                                 size_t length,
                                 void *response,
                                 size_t *response_len,
                                 uint32_t timeout_ms)
{
    if (!state.initialized) {
        return PROTO_ERR_INVALID_STATE;
    }
    
    if (timeout_ms == 0) {
        timeout_ms = STM32_PROTOCOL_TIMEOUT_MS;
    }
    
    os_mutex_take(state.pending_mutex, OS_WAIT_FOREVER);
    
    // Allocate pending command slot
    pending_command_t *pending = alloc_pending_command();
    if (pending == NULL) {
        os_mutex_give(state.pending_mutex);
        LOG_E(TAG, "No pending command slots available");
        return PROTO_ERR_NO_MEM;
    }
    
    // Set up pending command
    pending->seq = get_next_seq();
    pending->cmd_id = cmd;
    pending->send_time = os_get_time_ms();
    pending->timeout_ms = timeout_ms;
    pending->retries_left = STM32_PROTOCOL_MAX_RETRIES;
    pending->waiting = true;
    pending->response_sem = os_semaphore_create_binary();
    pending->response_buffer = (uint8_t*)response;
    pending->response_len = response_len;
    pending->response_max_len = (response_len != NULL) ? *response_len : 0;
    pending->callback = NULL;
    pending->user_data = NULL;

    // Store payload for retries
    if (payload != NULL && length > 0) {
        memcpy(pending->payload, payload, length);
    }
    pending->payload_len = length;

    if (response_len != NULL) {
        *response_len = 0;
    }

    // Send command
    proto_err_t err = send_command_packet(cmd, pending->seq, payload, length);
    if (err != PROTO_OK) {
        free_pending_command(pending);
        os_mutex_give(state.pending_mutex);
        return -err;
    }
    
    os_mutex_give(state.pending_mutex);
    
    // Wait for response
    os_result_t result = os_semaphore_take(pending->response_sem, os_ms_to_ticks(timeout_ms + 500));
    
    os_mutex_take(state.pending_mutex, OS_WAIT_FOREVER);
    
    int status;
    if (result == OS_SUCCESS) {
        status = pending->response_status;
    } else {
        status = PROTO_ERR_TIMEOUT;
    }
    
    free_pending_command(pending);
    os_mutex_give(state.pending_mutex);
    
    return status;
}

proto_err_t stm32_protocol_send_command_async(stm32_command_id_t cmd,
                                             const void *payload,
                                             size_t length,
                                             stm32_response_callback_t callback,
                                             void *user_data)
{
    if (!state.initialized) {
        return PROTO_ERR_INVALID_STATE;
    }
    
    os_mutex_take(state.pending_mutex, OS_WAIT_FOREVER);
    
    // Allocate pending command slot
    pending_command_t *pending = alloc_pending_command();
    if (pending == NULL) {
        os_mutex_give(state.pending_mutex);
        LOG_E(TAG, "No pending command slots available");
        return PROTO_ERR_NO_MEM;
    }
    
    // Set up pending command
    pending->seq = get_next_seq();
    pending->cmd_id = cmd;
    pending->send_time = os_get_time_ms();
    pending->timeout_ms = STM32_PROTOCOL_TIMEOUT_MS;
    pending->retries_left = STM32_PROTOCOL_MAX_RETRIES;
    pending->waiting = false;
    pending->callback = callback;
    pending->user_data = user_data;

    // Store payload for retries
    if (payload != NULL && length > 0) {
        memcpy(pending->payload, payload, length);
    }
    pending->payload_len = length;

    // Send command
    proto_err_t err = send_command_packet(cmd, pending->seq, payload, length);
    if (err != PROTO_OK) {
        free_pending_command(pending);
        os_mutex_give(state.pending_mutex);
        return err;
    }
    
    os_mutex_give(state.pending_mutex);
    
    return PROTO_OK;
}

proto_err_t stm32_protocol_register_notify_callback(stm32_notify_callback_t callback,
                                                     void *user_data)
{
    state.notify_callback = callback;
    state.notify_user_data = user_data;
    return PROTO_OK;
}

bool stm32_protocol_is_ready(void)
{
    return state.initialized && state.running;
}

void stm32_protocol_get_stats(uint32_t *commands_sent,
                               uint32_t *responses_received,
                               uint32_t *timeouts,
                               uint32_t *retries)
{
    if (commands_sent) *commands_sent = state.commands_sent;
    if (responses_received) *responses_received = state.responses_received;
    if (timeouts) *timeouts = state.timeouts;
    if (retries) *retries = state.retries;
}

void stm32_protocol_reset_stats(void)
{
    state.commands_sent = 0;
    state.responses_received = 0;
    state.timeouts = 0;
    state.retries = 0;
}

// ============================================================================
// Helper Functions for Common Commands
// ============================================================================

proto_err_t stm32_cmd_get_buffer_data(uint32_t start_index,
                                       uint32_t count,
                                       stm32_response_callback_t callback,
                                       void *user_data)
{
    struct {
        uint32_t start_index;
        uint32_t count;
    } __attribute__((packed)) payload = {
        .start_index = start_index,
        .count = count
    };
    
    return stm32_protocol_send_command_async(CMD_GET_BUFFER_DATA,
                                             &payload, sizeof(payload),
                                             callback, user_data);
}

proto_err_t stm32_cmd_start_measurement(uint32_t interval_ms,
                                         stm32_response_callback_t callback,
                                         void *user_data)
{
    uint32_t payload = interval_ms;
    
    return stm32_protocol_send_command_async(CMD_START_MEASUREMENT,
                                             &payload, sizeof(payload),
                                             callback, user_data);
}

proto_err_t stm32_cmd_stop_measurement(stm32_response_callback_t callback,
                                        void *user_data)
{
    return stm32_protocol_send_command_async(CMD_STOP_MEASUREMENT,
                                             NULL, 0,
                                             callback, user_data);
}

proto_err_t stm32_cmd_set_rtc(uint32_t unix_time,
                               stm32_response_callback_t callback,
                               void *user_data)
{
    return stm32_protocol_send_command_async(CMD_SET_RTC,
                                             &unix_time, sizeof(unix_time),
                                             callback, user_data);
}

proto_err_t stm32_cmd_get_status(stm32_response_callback_t callback,
                                  void *user_data)
{
    return stm32_protocol_send_command_async(CMD_GET_STATUS,
                                             NULL, 0,
                                             callback, user_data);
}
