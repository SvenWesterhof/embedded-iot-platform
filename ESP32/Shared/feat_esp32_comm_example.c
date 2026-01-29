/**
 * @file feat_esp32_comm_example.c
 * @brief Example: How to use feat_esp32_comm on STM32 side
 *
 * This shows how to implement command handlers on the STM32.
 * Adapt to your actual STM32 application.
 */

#include "feat_esp32_comm.h"
#include "protocol_common.h"
#include <string.h>

// Your application state
static struct {
    bool measuring;
    uint32_t interval_ms;
    uint32_t rtc_time;
    uint32_t uptime;
    uint16_t buffer_count;
} app_state = {0};

/**
 * @brief Command handler - processes commands from ESP32
 */
static response_status_t cmd_handler(
    command_id_t cmd_id,
    const uint8_t *payload,
    size_t payload_len,
    uint8_t *response,
    size_t *response_len,
    size_t max_response)
{
    *response_len = 0;

    switch (cmd_id) {
        case CMD_GET_STATUS: {
            // Build status response
            resp_get_status_t status_resp = {
                .state = app_state.measuring ? 1 : 0,
                .error_code = 0,
                .buffer_count = app_state.buffer_count,
                .uptime_sec = app_state.uptime,
            };

            if (max_response >= sizeof(status_resp)) {
                memcpy(response, &status_resp, sizeof(status_resp));
                *response_len = sizeof(status_resp);
            }
            return RESP_OK;
        }

        case CMD_START_MEASUREMENT: {
            if (payload_len < sizeof(cmd_start_measurement_t)) {
                return RESP_INVALID_PARAM;
            }

            cmd_start_measurement_t *cmd = (cmd_start_measurement_t *)payload;
            app_state.measuring = true;
            app_state.interval_ms = cmd->interval_ms;

            // TODO: Start your actual measurement here
            // start_adc_sampling(cmd->interval_ms);

            return RESP_OK;
        }

        case CMD_STOP_MEASUREMENT: {
            app_state.measuring = false;

            // TODO: Stop your actual measurement here
            // stop_adc_sampling();

            return RESP_OK;
        }

        case CMD_SET_RTC: {
            if (payload_len < sizeof(cmd_set_rtc_t)) {
                return RESP_INVALID_PARAM;
            }

            cmd_set_rtc_t *cmd = (cmd_set_rtc_t *)payload;
            app_state.rtc_time = cmd->unix_time;

            // TODO: Set actual RTC
            // RTC_SetTime(cmd->unix_time);

            return RESP_OK;
        }

        case CMD_GET_BUFFER_DATA: {
            if (payload_len < sizeof(cmd_get_buffer_data_t)) {
                return RESP_INVALID_PARAM;
            }

            cmd_get_buffer_data_t *cmd = (cmd_get_buffer_data_t *)payload;
            (void)cmd;  // Suppress unused warning

            // TODO: Read from your data buffer
            if (app_state.buffer_count == 0) {
                return RESP_NO_DATA;
            }

            // Example: return some data
            // size_t data_len = read_buffer(cmd->start_index, cmd->count, response, max_response);
            // *response_len = data_len;

            return RESP_OK;
        }

        case CMD_CLEAR_BUFFER: {
            app_state.buffer_count = 0;
            // TODO: clear_data_buffer();
            return RESP_OK;
        }

        case CMD_GET_CONFIG:
        case CMD_SET_CONFIG:
            // TODO: Implement config handling
            return RESP_INVALID_CMD;

        default:
            return RESP_INVALID_CMD;
    }
}

/**
 * @brief Initialize and start ESP32 communication
 *
 * Call this from your main() after FreeRTOS scheduler starts
 */
void app_esp32_comm_init(void)
{
    // Initialize communication feature
    if (feat_esp32_comm_init() != 0) {
        // Handle error
        return;
    }

    // Register command handler
    feat_esp32_comm_register_handler(cmd_handler);

    // Start communication task
    if (feat_esp32_comm_start() != 0) {
        // Handle error
        return;
    }

    // Communication is now running and will handle incoming commands
}

/**
 * @brief Example: Send notification to ESP32
 *
 * Call this when you have new data to report
 */
void app_send_data_notification(uint8_t *data, size_t len)
{
    // Send notification with new data
    feat_esp32_comm_send_notify(CMD_GET_BUFFER_DATA, data, len);
}

/**
 * @brief Example main loop (or task)
 */
void app_main_loop(void)
{
    while (1) {
        // Your application logic
        app_state.uptime++;

        // If measuring, collect data
        if (app_state.measuring) {
            // uint16_t sample = read_adc();
            // store_sample(sample);
            // app_state.buffer_count++;

            // Optionally notify ESP32 of new data
            // app_send_data_notification(&sample, sizeof(sample));
        }

        os_delay_ms(1000);
    }
}
