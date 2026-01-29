/**
 * @file protocol_handler.c
 * @brief Protocol handler implementation for ESP32-STM32 communication
 */

#include "protocol_handler.h"
#include "esp32_packet_framing.h"
#include "portable_log.h"
#include "os_wrapper.h"
#include "serv_temperature_sensor.h"
#include "serv_current_monitor.h"
#include "event_bus.h"
#include "service_events.h"
#include "hal_rtc.h"
#include <string.h>

static const char *TAG = "PROTO";

// ============================================================================
// Configuration
// ============================================================================

#define STREAM_TASK_STACK_SIZE  4096
#define STREAM_TASK_PRIORITY    8

// ============================================================================
// Internal State
// ============================================================================

typedef struct {
    bool initialized;
    uint8_t seq_counter;              // For notifications

    // Streaming state
    bool streaming_active;
    sensor_type_t stream_sensor;
    uint32_t stream_interval_ms;
    os_task_handle_t stream_task_handle;
    volatile bool stream_stop_requested;

    // Latest temperature data (updated via event bus)
    float last_temperature;
    float last_humidity;
    bool temp_data_valid;
} protocol_state_t;

static protocol_state_t state = {0};

// ============================================================================
// Forward Declarations
// ============================================================================

static void packet_rx_callback(stm32_uart_event_t *event, void *user_data);
static void handle_command(const protocol_packet_t *packet);
static void handle_cmd_get_status(const protocol_packet_t *cmd);
static void handle_cmd_set_rtc(const protocol_packet_t *cmd);
static void handle_cmd_start_measurement(const protocol_packet_t *cmd);
static void handle_cmd_stop_measurement(const protocol_packet_t *cmd);
static void handle_cmd_get_buffer_data(const protocol_packet_t *cmd);
static void handle_cmd_clear_buffer(const protocol_packet_t *cmd);
static void stream_task(void *param);
static void temperature_event_handler(event_t *event);

// ============================================================================
// Public API
// ============================================================================

proto_handler_status_t protocol_handler_init(void)
{
    if (state.initialized) {
        return PROTO_HANDLER_ERR_ALREADY_INIT;
    }

    // Initialize packet framing layer
    stm32_uart_config_t uart_config = stm32_uart_get_default_config();
    uart_config.callback = packet_rx_callback;
    uart_config.user_data = NULL;

    uart_driver_status_t uart_status = stm32_uart_init(&uart_config);
    if (uart_status != UART_DRV_OK) {
        LOG_E(TAG, "Failed to init packet framing: %d", uart_status);
        return PROTO_HANDLER_ERR_NOT_INIT;
    }

    // Subscribe to temperature events
    event_bus_subscribe(EVENT_TEMPERATURE_UPDATED, temperature_event_handler);

    state.seq_counter = 0;
    state.streaming_active = false;
    state.stream_task_handle = NULL;
    state.stream_stop_requested = false;
    state.temp_data_valid = false;

    state.initialized = true;
    LOG_I(TAG, "Protocol handler initialized");

    return PROTO_HANDLER_OK;
}

proto_handler_status_t protocol_handler_deinit(void)
{
    if (!state.initialized) {
        return PROTO_HANDLER_ERR_NOT_INIT;
    }

    // Stop streaming if active
    protocol_handler_stop_stream();

    // Unsubscribe from events
    event_bus_unsubscribe(EVENT_TEMPERATURE_UPDATED, temperature_event_handler);

    // Deinit packet framing
    stm32_uart_deinit();

    state.initialized = false;
    LOG_I(TAG, "Protocol handler deinitialized");

    return PROTO_HANDLER_OK;
}

bool protocol_handler_is_initialized(void)
{
    return state.initialized;
}

proto_handler_status_t protocol_handler_send_response(
    uint8_t cmd_id,
    uint8_t seq,
    response_status_t status,
    const void *payload,
    uint16_t payload_len)
{
    if (!state.initialized) {
        return PROTO_HANDLER_ERR_NOT_INIT;
    }

    protocol_packet_t resp = {0};
    resp.type = PACKET_TYPE_RESP;
    resp.cmd_id = cmd_id;
    resp.seq = seq;
    resp.status = status;
    resp.length = payload_len;

    if (payload != NULL && payload_len > 0) {
        if (payload_len > PROTOCOL_MAX_PAYLOAD_SIZE) {
            return PROTO_HANDLER_ERR_INVALID_PARAM;
        }
        memcpy(resp.payload, payload, payload_len);
    }

    size_t total_len = PROTOCOL_HEADER_SIZE + payload_len;
    uart_driver_status_t tx_status = stm32_uart_send_packet_async(
        (const uint8_t *)&resp, total_len);

    if (tx_status != UART_DRV_OK) {
        LOG_E(TAG, "Failed to send response: %d", tx_status);
        return PROTO_HANDLER_ERR_TX_FAILED;
    }

    LOG_D(TAG, "Response sent: cmd=0x%02X seq=%d status=%d", cmd_id, seq, status);
    return PROTO_HANDLER_OK;
}

proto_handler_status_t protocol_handler_send_notification(
    uint8_t cmd_id,
    const void *payload,
    uint16_t payload_len)
{
    if (!state.initialized) {
        return PROTO_HANDLER_ERR_NOT_INIT;
    }

    protocol_packet_t notify = {0};
    notify.type = PACKET_TYPE_NOTIFY;
    notify.cmd_id = cmd_id;
    notify.seq = state.seq_counter++;
    notify.status = RESP_OK;
    notify.length = payload_len;

    if (payload != NULL && payload_len > 0) {
        if (payload_len > PROTOCOL_MAX_PAYLOAD_SIZE) {
            return PROTO_HANDLER_ERR_INVALID_PARAM;
        }
        memcpy(notify.payload, payload, payload_len);
    }

    size_t total_len = PROTOCOL_HEADER_SIZE + payload_len;
    uart_driver_status_t tx_status = stm32_uart_send_packet_async(
        (const uint8_t *)&notify, total_len);

    if (tx_status != UART_DRV_OK) {
        LOG_W(TAG, "Failed to send notification: %d", tx_status);
        return PROTO_HANDLER_ERR_TX_FAILED;
    }

    return PROTO_HANDLER_OK;
}

proto_handler_status_t protocol_handler_send_sensor_sample(
    const sensor_sample_t *sample)
{
    if (sample == NULL) {
        return PROTO_HANDLER_ERR_INVALID_PARAM;
    }

    return protocol_handler_send_notification(
        NOTIFY_SENSOR_DATA,
        sample,
        sizeof(sensor_sample_t));
}

proto_handler_status_t protocol_handler_start_stream(
    sensor_type_t sensor_type,
    uint32_t interval_ms)
{
    if (!state.initialized) {
        return PROTO_HANDLER_ERR_NOT_INIT;
    }

    // Stop existing stream if any
    if (state.streaming_active) {
        protocol_handler_stop_stream();
    }

    state.stream_sensor = sensor_type;
    state.stream_interval_ms = interval_ms;
    state.stream_stop_requested = false;

    // Create streaming task
    os_result_t ret = os_task_create(
        stream_task,
        "proto_stream",
        STREAM_TASK_STACK_SIZE,
        NULL,
        STREAM_TASK_PRIORITY,
        &state.stream_task_handle);

    if (ret != OS_SUCCESS) {
        LOG_E(TAG, "Failed to create stream task");
        return PROTO_HANDLER_ERR_NOT_INIT;
    }

    state.streaming_active = true;
    LOG_I(TAG, "Started streaming sensor %d @ %lu ms", sensor_type, interval_ms);

    return PROTO_HANDLER_OK;
}

proto_handler_status_t protocol_handler_stop_stream(void)
{
    if (!state.streaming_active) {
        return PROTO_HANDLER_OK;
    }

    state.stream_stop_requested = true;

    // Wait for task to finish (with timeout)
    for (int i = 0; i < 100 && state.streaming_active; i++) {
        os_delay_ms(10);
    }

    // Task deletes itself when it exits, so only force-delete if it didn't exit cleanly
    if (state.streaming_active && state.stream_task_handle != NULL) {
        os_task_delete(state.stream_task_handle);
    }
    state.stream_task_handle = NULL;
    state.streaming_active = false;

    LOG_I(TAG, "Stopped streaming");

    return PROTO_HANDLER_OK;
}

bool protocol_handler_is_streaming(void)
{
    return state.streaming_active;
}

// ============================================================================
// Internal Functions
// ============================================================================

static void packet_rx_callback(stm32_uart_event_t *event, void *user_data)
{
    (void)user_data;

    if (event->type != STM32_UART_EVENT_PACKET_RECEIVED) {
        return;
    }

    if (event->data == NULL || event->length < PROTOCOL_HEADER_SIZE) {
        LOG_W(TAG, "Invalid packet: len=%u", event->length);
        return;
    }

    const protocol_packet_t *packet = (const protocol_packet_t *)event->data;

    // Verify packet type
    if (packet->type != PACKET_TYPE_CMD) {
        LOG_W(TAG, "Unexpected packet type: 0x%02X", packet->type);
        return;
    }

    // Verify length
    if (packet->length > event->length - PROTOCOL_HEADER_SIZE) {
        LOG_W(TAG, "Payload length mismatch");
        return;
    }

    handle_command(packet);
}

static void handle_command(const protocol_packet_t *packet)
{
    LOG_D(TAG, "CMD: id=0x%02X seq=%d len=%d", packet->cmd_id, packet->seq, packet->length);

    switch (packet->cmd_id) {
        case CMD_GET_STATUS:
            handle_cmd_get_status(packet);
            break;

        case CMD_SET_RTC:
            handle_cmd_set_rtc(packet);
            break;

        case CMD_START_MEASUREMENT:
            handle_cmd_start_measurement(packet);
            break;

        case CMD_STOP_MEASUREMENT:
            handle_cmd_stop_measurement(packet);
            break;

        case CMD_GET_BUFFER_DATA:
            handle_cmd_get_buffer_data(packet);
            break;

        case CMD_CLEAR_BUFFER:
            handle_cmd_clear_buffer(packet);
            break;

        default:
            LOG_W(TAG, "Unknown command: 0x%02X", packet->cmd_id);
            protocol_handler_send_response(
                packet->cmd_id, packet->seq, RESP_INVALID_CMD, NULL, 0);
            break;
    }
}

static void handle_cmd_get_status(const protocol_packet_t *cmd)
{
    resp_get_status_t status_resp = {0};

    // Get current monitor status
    measurement_status_t meas_status = current_monitor_get_status();
    status_resp.state = (uint8_t)meas_status;
    status_resp.error_code = 0;

    // Get temperature buffer count
    status_resp.buffer_count = (uint16_t)temperature_sensor_buffer_get_count();

    // Uptime
    status_resp.uptime_sec = os_get_tick_count() / 1000;

    protocol_handler_send_response(
        cmd->cmd_id, cmd->seq, RESP_OK,
        &status_resp, sizeof(status_resp));
}

static void handle_cmd_set_rtc(const protocol_packet_t *cmd)
{
    if (cmd->length < sizeof(cmd_set_rtc_t)) {
        protocol_handler_send_response(
            cmd->cmd_id, cmd->seq, RESP_INVALID_PARAM, NULL, 0);
        return;
    }

    const cmd_set_rtc_t *rtc_cmd = (const cmd_set_rtc_t *)cmd->payload;

    hal_rtc_status_t rtc_status = hal_rtc_set_time(rtc_cmd->unix_time, 0);
    if (rtc_status != HAL_RTC_OK) {
        LOG_E(TAG, "Failed to set RTC: %d", rtc_status);
        protocol_handler_send_response(
            cmd->cmd_id, cmd->seq, RESP_ERROR, NULL, 0);
        return;
    }

    LOG_I(TAG, "RTC set to: %lu", rtc_cmd->unix_time);

    protocol_handler_send_response(
        cmd->cmd_id, cmd->seq, RESP_OK, NULL, 0);
}

static void handle_cmd_start_measurement(const protocol_packet_t *cmd)
{
    if (cmd->length < sizeof(cmd_start_stream_t)) {
        protocol_handler_send_response(
            cmd->cmd_id, cmd->seq, RESP_INVALID_PARAM, NULL, 0);
        return;
    }

    const cmd_start_stream_t *stream_cmd = (const cmd_start_stream_t *)cmd->payload;

    proto_handler_status_t status = protocol_handler_start_stream(
        (sensor_type_t)stream_cmd->sensor_type,
        stream_cmd->interval_ms);

    protocol_handler_send_response(
        cmd->cmd_id, cmd->seq,
        (status == PROTO_HANDLER_OK) ? RESP_OK : RESP_ERROR,
        NULL, 0);
}

static void handle_cmd_stop_measurement(const protocol_packet_t *cmd)
{
    protocol_handler_stop_stream();

    protocol_handler_send_response(
        cmd->cmd_id, cmd->seq, RESP_OK, NULL, 0);
}

static void handle_cmd_get_buffer_data(const protocol_packet_t *cmd)
{
    if (cmd->length < sizeof(cmd_get_buffer_data_t)) {
        protocol_handler_send_response(
            cmd->cmd_id, cmd->seq, RESP_INVALID_PARAM, NULL, 0);
        return;
    }

    const cmd_get_buffer_data_t *req = (const cmd_get_buffer_data_t *)cmd->payload;

    // Check if there's data available
    uint32_t available = temperature_sensor_buffer_get_count();
    if (available == 0) {
        protocol_handler_send_response(
            cmd->cmd_id, cmd->seq, RESP_NO_DATA, NULL, 0);
        return;
    }

    // Calculate how many samples we can fit in response
    // Response format: resp_buffer_data_header_t + sensor_sample_t[]
    uint32_t max_samples_in_payload =
        (PROTOCOL_MAX_PAYLOAD_SIZE - sizeof(resp_buffer_data_header_t)) / sizeof(sensor_sample_t);

    uint32_t samples_to_read = req->count;
    if (samples_to_read > max_samples_in_payload) {
        samples_to_read = max_samples_in_payload;
    }

    // Prepare response buffer
    uint8_t response_buf[PROTOCOL_MAX_PAYLOAD_SIZE];
    resp_buffer_data_header_t *header = (resp_buffer_data_header_t *)response_buf;
    sensor_sample_t *samples = (sensor_sample_t *)(response_buf + sizeof(resp_buffer_data_header_t));

    // Read samples from buffer
    uint32_t samples_read = 0;
    bool success = temperature_sensor_buffer_read(
        req->start_index, samples, samples_to_read, &samples_read);

    if (!success || samples_read == 0) {
        protocol_handler_send_response(
            cmd->cmd_id, cmd->seq, RESP_NO_DATA, NULL, 0);
        return;
    }

    // Fill header
    header->sensor_type = SENSOR_TEMPERATURE;
    header->sample_count = (uint16_t)samples_read;

    // Calculate total response size
    uint16_t payload_len = sizeof(resp_buffer_data_header_t) + (samples_read * sizeof(sensor_sample_t));

    protocol_handler_send_response(
        cmd->cmd_id, cmd->seq, RESP_OK, response_buf, payload_len);
}

static void handle_cmd_clear_buffer(const protocol_packet_t *cmd)
{
    current_monitor_clear();
    temperature_sensor_buffer_clear();

    protocol_handler_send_response(
        cmd->cmd_id, cmd->seq, RESP_OK, NULL, 0);
}

static void stream_task(void *param)
{
    (void)param;

    LOG_I(TAG, "Stream task started: sensor=%d interval=%lu",
          state.stream_sensor, state.stream_interval_ms);

    while (!state.stream_stop_requested) {
        sensor_sample_t sample = {0};
        sample.sensor_type = state.stream_sensor;
        sample.timestamp = os_get_tick_count();

        switch (state.stream_sensor) {
            case SENSOR_TEMPERATURE:
                if (state.temp_data_valid) {
                    // Temperature in centi-degrees (e.g., 2350 = 23.50 C)
                    sample.value = (int32_t)(state.last_temperature * 100);
                } else {
                    sample.value = 0;
                }
                break;

            case SENSOR_CURRENT: {
                INA226_Data ina_data;
                if (current_monitor_get_instant_reading(&ina_data)) {
                    // Current in microamps
                    sample.value = (int32_t)(ina_data.current_mA * 1000);
                } else {
                    sample.value = 0;
                }
                break;
            }

            default:
                sample.value = 0;
                break;
        }

        protocol_handler_send_sensor_sample(&sample);

        os_delay_ms(state.stream_interval_ms);
    }

    LOG_I(TAG, "Stream task exiting");
    state.streaming_active = false;
    os_task_delete(NULL);  // Delete self
}

static void temperature_event_handler(event_t *event)
{
    if (event == NULL || event->data == NULL) {
        return;
    }

    const temperature_data_t *temp_data = (const temperature_data_t *)event->data;

    if (temp_data->sensor_ok) {
        state.last_temperature = temp_data->temperature;
        state.last_humidity = temp_data->humidity;
        state.temp_data_valid = true;
    } else {
        state.temp_data_valid = false;
    }
}
