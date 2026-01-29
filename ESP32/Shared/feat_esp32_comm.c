/**
 * @file feat_esp32_comm.c
 * @brief STM32 Feature: ESP32 Communication Handler Implementation
 *
 * Copy to STM32 project: Middleware/Features/
 *
 * Requires:
 *   - hal_uart.h/c implementation for STM32
 *   - os_wrapper.h/c implementation for STM32 FreeRTOS
 */

#include "feat_esp32_comm.h"
#include "hal_uart.h"
#include "os_wrapper.h"
#include <string.h>

// ============================================================================
// Configuration - Adjust for your STM32 setup
// ============================================================================

#define UART_PORT           1       // UART port number
#define UART_BAUD_RATE      115200
#define RX_BUFFER_SIZE      2048
#define TX_BUFFER_SIZE      1024
#define MAX_PACKET_SIZE     512

// Packet framing
#define RX_POLL_INTERVAL_MS 5

// ============================================================================
// CRC16-CCITT (same as ESP32 side)
// ============================================================================

static const uint16_t crc16_table[256] = {
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
    0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF,
    0x1231, 0x0210, 0x3273, 0x2252, 0x52B5, 0x4294, 0x72F7, 0x62D6,
    0x9339, 0x8318, 0xB37B, 0xA35A, 0xD3BD, 0xC39C, 0xF3FF, 0xE3DE,
    0x2462, 0x3443, 0x0420, 0x1401, 0x64E6, 0x74C7, 0x44A4, 0x5485,
    0xA56A, 0xB54B, 0x8528, 0x9509, 0xE5EE, 0xF5CF, 0xC5AC, 0xD58D,
    0x3653, 0x2672, 0x1611, 0x0630, 0x76D7, 0x66F6, 0x5695, 0x46B4,
    0xB75B, 0xA77A, 0x9719, 0x8738, 0xF7DF, 0xE7FE, 0xD79D, 0xC7BC,
    0x48C4, 0x58E5, 0x6886, 0x78A7, 0x0840, 0x1861, 0x2802, 0x3823,
    0xC9CC, 0xD9ED, 0xE98E, 0xF9AF, 0x8948, 0x9969, 0xA90A, 0xB92B,
    0x5AF5, 0x4AD4, 0x7AB7, 0x6A96, 0x1A71, 0x0A50, 0x3A33, 0x2A12,
    0xDBFD, 0xCBDC, 0xFBBF, 0xEB9E, 0x9B79, 0x8B58, 0xBB3B, 0xAB1A,
    0x6CA6, 0x7C87, 0x4CE4, 0x5CC5, 0x2C22, 0x3C03, 0x0C60, 0x1C41,
    0xEDAE, 0xFD8F, 0xCDEC, 0xDDCD, 0xAD2A, 0xBD0B, 0x8D68, 0x9D49,
    0x7E97, 0x6EB6, 0x5ED5, 0x4EF4, 0x3E13, 0x2E32, 0x1E51, 0x0E70,
    0xFF9F, 0xEFBE, 0xDFDD, 0xCFFC, 0xBF1B, 0xAF3A, 0x9F59, 0x8F78,
    0x9188, 0x81A9, 0xB1CA, 0xA1EB, 0xD10C, 0xC12D, 0xF14E, 0xE16F,
    0x1080, 0x00A1, 0x30C2, 0x20E3, 0x5004, 0x4025, 0x7046, 0x6067,
    0x83B9, 0x9398, 0xA3FB, 0xB3DA, 0xC33D, 0xD31C, 0xE37F, 0xF35E,
    0x02B1, 0x1290, 0x22F3, 0x32D2, 0x4235, 0x5214, 0x6277, 0x7256,
    0xB5EA, 0xA5CB, 0x95A8, 0x8589, 0xF56E, 0xE54F, 0xD52C, 0xC50D,
    0x34E2, 0x24C3, 0x14A0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
    0xA7DB, 0xB7FA, 0x8799, 0x97B8, 0xE75F, 0xF77E, 0xC71D, 0xD73C,
    0x26D3, 0x36F2, 0x0691, 0x16B0, 0x6657, 0x7676, 0x4615, 0x5634,
    0xD94C, 0xC96D, 0xF90E, 0xE92F, 0x99C8, 0x89E9, 0xB98A, 0xA9AB,
    0x5844, 0x4865, 0x7806, 0x6827, 0x18C0, 0x08E1, 0x3882, 0x28A3,
    0xCB7D, 0xDB5C, 0xEB3F, 0xFB1E, 0x8BF9, 0x9BD8, 0xABBB, 0xBB9A,
    0x4A75, 0x5A54, 0x6A37, 0x7A16, 0x0AF1, 0x1AD0, 0x2AB3, 0x3A92,
    0xFD2E, 0xED0F, 0xDD6C, 0xCD4D, 0xBDAA, 0xAD8B, 0x9DE8, 0x8DC9,
    0x7C26, 0x6C07, 0x5C64, 0x4C45, 0x3CA2, 0x2C83, 0x1CE0, 0x0CC1,
    0xEF1F, 0xFF3E, 0xCF5D, 0xDF7C, 0xAF9B, 0xBFBA, 0x8FD9, 0x9FF8,
    0x6E17, 0x7E36, 0x4E55, 0x5E74, 0x2E93, 0x3EB2, 0x0ED1, 0x1EF0
};

static uint16_t crc16_ccitt(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < length; i++) {
        uint8_t index = (uint8_t)((crc >> 8) ^ data[i]);
        crc = (crc << 8) ^ crc16_table[index];
    }
    return crc;
}

// ============================================================================
// Internal State
// ============================================================================

typedef enum {
    RX_STATE_IDLE,
    RX_STATE_LENGTH_LOW,
    RX_STATE_LENGTH_HIGH,
    RX_STATE_DATA,
    RX_STATE_CRC_LOW,
    RX_STATE_CRC_HIGH,
    RX_STATE_END,
} rx_state_t;

typedef struct {
    bool initialized;
    bool running;

    // RX state machine
    rx_state_t rx_state;
    uint8_t rx_buffer[MAX_PACKET_SIZE];
    size_t rx_index;
    uint16_t rx_expected_length;
    uint16_t rx_crc;

    // Task
    os_task_handle_t comm_task;
    os_mutex_handle_t tx_mutex;

    // Callback
    esp32_cmd_handler_t cmd_handler;

    // Stats
    uint32_t commands_received;
    uint32_t responses_sent;
    uint32_t errors;
} comm_state_t;

static comm_state_t state = {0};

// ============================================================================
// Internal Functions
// ============================================================================

static void rx_reset_state(void)
{
    state.rx_state = RX_STATE_IDLE;
    state.rx_index = 0;
    state.rx_expected_length = 0;
    state.rx_crc = 0;
}

/**
 * @brief Send a framed packet
 */
static int send_packet(const uint8_t *data, size_t length)
{
    if (length > MAX_PACKET_SIZE - 6) {
        return -1;
    }

    uint8_t tx_buffer[MAX_PACKET_SIZE];
    size_t tx_index = 0;

    // Start marker
    tx_buffer[tx_index++] = PACKET_START_MARKER;

    // Length (little-endian)
    tx_buffer[tx_index++] = (uint8_t)(length & 0xFF);
    tx_buffer[tx_index++] = (uint8_t)((length >> 8) & 0xFF);

    // Data
    memcpy(&tx_buffer[tx_index], data, length);
    tx_index += length;

    // CRC
    uint16_t crc = crc16_ccitt(data, length);
    tx_buffer[tx_index++] = (uint8_t)(crc & 0xFF);
    tx_buffer[tx_index++] = (uint8_t)((crc >> 8) & 0xFF);

    // End marker
    tx_buffer[tx_index++] = PACKET_END_MARKER;

    // Send
    os_mutex_take(state.tx_mutex, OS_WAIT_FOREVER);
    int sent = hal_uart_write(UART_PORT, tx_buffer, tx_index, 100);
    hal_uart_flush_tx(UART_PORT, 100);
    os_mutex_give(state.tx_mutex);

    return (sent == (int)tx_index) ? 0 : -1;
}

/**
 * @brief Send response to a command
 */
static void send_response(uint8_t cmd_id, uint8_t seq, response_status_t status,
                          const uint8_t *payload, size_t length)
{
    protocol_packet_t resp;
    resp.type = PACKET_TYPE_RESP;
    resp.cmd_id = cmd_id;
    resp.seq = seq;
    resp.status = (uint8_t)status;
    resp.length = length;

    if (payload != NULL && length > 0) {
        memcpy(resp.payload, payload, length);
    }

    size_t packet_size = PROTOCOL_HEADER_SIZE + length;

    if (send_packet((uint8_t *)&resp, packet_size) == 0) {
        state.responses_sent++;
    } else {
        state.errors++;
    }
}

/**
 * @brief Handle a received command packet
 */
static void handle_command(const uint8_t *data, size_t length)
{
    if (length < PROTOCOL_HEADER_SIZE) {
        state.errors++;
        return;
    }

    protocol_packet_t *pkt = (protocol_packet_t *)data;

    // Verify packet type
    if (pkt->type != PACKET_TYPE_CMD) {
        state.errors++;
        return;
    }

    // Verify length
    if (PROTOCOL_HEADER_SIZE + pkt->length != length) {
        state.errors++;
        return;
    }

    state.commands_received++;

    // Call handler
    uint8_t response_buf[PROTOCOL_MAX_PAYLOAD_SIZE];
    size_t response_len = 0;
    response_status_t status = RESP_INVALID_CMD;

    if (state.cmd_handler != NULL) {
        status = state.cmd_handler(
            (command_id_t)pkt->cmd_id,
            pkt->length > 0 ? pkt->payload : NULL,
            pkt->length,
            response_buf,
            &response_len,
            sizeof(response_buf)
        );
    }

    // Send response with same seq number
    send_response(pkt->cmd_id, pkt->seq, status, response_buf, response_len);
}

/**
 * @brief Process a received byte
 */
static void rx_process_byte(uint8_t byte)
{
    switch (state.rx_state) {
        case RX_STATE_IDLE:
            if (byte == PACKET_START_MARKER) {
                state.rx_state = RX_STATE_LENGTH_LOW;
                state.rx_index = 0;
            }
            break;

        case RX_STATE_LENGTH_LOW:
            state.rx_expected_length = byte;
            state.rx_state = RX_STATE_LENGTH_HIGH;
            break;

        case RX_STATE_LENGTH_HIGH:
            state.rx_expected_length |= (uint16_t)byte << 8;
            if (state.rx_expected_length > MAX_PACKET_SIZE - 6) {
                rx_reset_state();
            } else if (state.rx_expected_length == 0) {
                state.rx_state = RX_STATE_CRC_LOW;
            } else {
                state.rx_state = RX_STATE_DATA;
            }
            break;

        case RX_STATE_DATA:
            state.rx_buffer[state.rx_index++] = byte;
            if (state.rx_index >= state.rx_expected_length) {
                state.rx_state = RX_STATE_CRC_LOW;
            }
            break;

        case RX_STATE_CRC_LOW:
            state.rx_crc = byte;
            state.rx_state = RX_STATE_CRC_HIGH;
            break;

        case RX_STATE_CRC_HIGH:
            state.rx_crc |= (uint16_t)byte << 8;
            state.rx_state = RX_STATE_END;
            break;

        case RX_STATE_END:
            if (byte == PACKET_END_MARKER) {
                // Validate CRC
                uint16_t calc_crc = crc16_ccitt(state.rx_buffer, state.rx_index);
                if (calc_crc == state.rx_crc) {
                    // Valid packet - handle it
                    handle_command(state.rx_buffer, state.rx_index);
                } else {
                    state.errors++;
                }
            } else {
                state.errors++;
            }
            rx_reset_state();
            break;
    }
}

/**
 * @brief Communication task - listens for commands from ESP32
 */
static void comm_task(void *arg)
{
    uint8_t rx_byte;

    while (state.running) {
        int bytes_read = hal_uart_read(UART_PORT, &rx_byte, 1, RX_POLL_INTERVAL_MS);

        if (bytes_read > 0) {
            rx_process_byte(rx_byte);
        } else {
            os_delay_ms(1);
        }
    }

    os_task_delete(NULL);
}

// ============================================================================
// Public API
// ============================================================================

int feat_esp32_comm_init(void)
{
    if (state.initialized) {
        return -1;
    }

    // Initialize UART
    hal_uart_config_t uart_cfg = hal_uart_get_default_config();
    uart_cfg.baud_rate = UART_BAUD_RATE;
    uart_cfg.rx_buffer_size = RX_BUFFER_SIZE;
    uart_cfg.tx_buffer_size = TX_BUFFER_SIZE;
    // Set pins as needed for your STM32 board
    // uart_cfg.tx_pin = ...;
    // uart_cfg.rx_pin = ...;

    if (!hal_uart_init(UART_PORT, &uart_cfg)) {
        return -1;
    }

    // Create TX mutex
    state.tx_mutex = os_mutex_create();
    if (state.tx_mutex == NULL) {
        hal_uart_deinit(UART_PORT);
        return -1;
    }

    rx_reset_state();
    state.cmd_handler = NULL;
    state.commands_received = 0;
    state.responses_sent = 0;
    state.errors = 0;
    state.initialized = true;

    return 0;
}

int feat_esp32_comm_start(void)
{
    if (!state.initialized || state.running) {
        return -1;
    }

    state.running = true;

    os_result_t ret = os_task_create(
        comm_task,
        "esp32_comm",
        ESP32_COMM_TASK_STACK,
        NULL,
        ESP32_COMM_TASK_PRIORITY,
        &state.comm_task
    );

    if (ret != OS_SUCCESS) {
        state.running = false;
        return -1;
    }

    return 0;
}

void feat_esp32_comm_stop(void)
{
    if (!state.running) {
        return;
    }

    state.running = false;
    os_delay_ms(100);  // Let task exit

    if (state.comm_task != NULL) {
        state.comm_task = NULL;
    }
}

void feat_esp32_comm_register_handler(esp32_cmd_handler_t handler)
{
    state.cmd_handler = handler;
}

int feat_esp32_comm_send_notify(command_id_t cmd_id,
                                 const uint8_t *payload,
                                 size_t length)
{
    if (!state.initialized) {
        return -1;
    }

    protocol_packet_t notify;
    notify.type = PACKET_TYPE_NOTIFY;
    notify.cmd_id = (uint8_t)cmd_id;
    notify.seq = 0;  // Notifications don't need seq
    notify.status = 0;
    notify.length = length;

    if (payload != NULL && length > 0) {
        memcpy(notify.payload, payload, length);
    }

    size_t packet_size = PROTOCOL_HEADER_SIZE + length;

    return send_packet((uint8_t *)&notify, packet_size);
}

void feat_esp32_comm_get_stats(uint32_t *commands_received,
                                uint32_t *responses_sent,
                                uint32_t *errors)
{
    if (commands_received) *commands_received = state.commands_received;
    if (responses_sent) *responses_sent = state.responses_sent;
    if (errors) *errors = state.errors;
}
