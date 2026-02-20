/**
 * @file serv_stm32_packet_framing.c
 * @brief STM32 Packet Framing Service
 *
 * Implements packet-based UART communication service with:
 * - Packet framing (0xAA start, 0x55 end)
 * - CRC16-CCITT validation
 * - Background receive task
 * - Thread-safe transmission
 */

#include "serv_stm32_packet_framing.h"
#include "crc16.h"
#include "portable_log.h"
#include "../../Drivers_BSP/BSP/pinout.h"
#include "../../HAL_Wrapper/hal_uart.h"
#include "os_wrapper.h"
#include <string.h>

static const char *TAG = "STM32_FRAMING";

// ============================================================================
// Internal Constants
// ============================================================================

#define RX_TASK_STACK_SIZE      4096
#define RX_TASK_PRIORITY        10
#define RX_POLL_INTERVAL_MS     5
#define TX_MUTEX_TIMEOUT_MS     1000

// Packet structure overhead: START(1) + LENGTH(2) + DATA + CRC(2) + END(1)
#define PACKET_OVERHEAD         6
#define PACKET_LENGTH_OFFSET    1
#define PACKET_DATA_OFFSET      3

// ============================================================================
// Internal State
// ============================================================================

typedef enum {
    RX_STATE_IDLE,              /**< Waiting for start marker */
    RX_STATE_LENGTH_LOW,        /**< Waiting for length low byte */
    RX_STATE_LENGTH_HIGH,       /**< Waiting for length high byte */
    RX_STATE_DATA,              /**< Receiving payload data */
    RX_STATE_CRC_LOW,           /**< Waiting for CRC low byte */
    RX_STATE_CRC_HIGH,          /**< Waiting for CRC high byte */
    RX_STATE_END,               /**< Waiting for end marker */
} rx_state_t;

typedef struct {
    bool initialized;
    stm32_framing_config_t config;

    // Receive state machine
    rx_state_t rx_state;
    uint8_t rx_buffer[STM32_UART_MAX_PACKET_SIZE];
    size_t rx_index;
    uint16_t rx_expected_length;
    uint16_t rx_crc;
    uint32_t rx_last_byte_time;

    // Tasks and synchronization
    os_task_handle_t rx_task_handle;
    os_mutex_handle_t tx_mutex;

    // Statistics
    stm32_framing_stats_t stats;
} stm32_framing_state_t;

static stm32_framing_state_t state = {0};

// ============================================================================
// Internal Functions
// ============================================================================

/**
 * @brief Reset receive state machine
 */
static void rx_reset_state(void)
{
    state.rx_state = RX_STATE_IDLE;
    state.rx_index = 0;
    state.rx_expected_length = 0;
    state.rx_crc = 0;
}

/**
 * @brief Notify callback of an event
 */
static void notify_event(stm32_framing_event_type_t type, uint8_t *data, size_t length)
{
    if (state.config.callback != NULL) {
        stm32_framing_event_t event = {
            .type = type,
            .data = data,
            .length = length
        };
        state.config.callback(&event, state.config.user_data);
    }
}

/**
 * @brief Process a received byte through the state machine
 */
static void rx_process_byte(uint8_t byte)
{
    uint32_t now = os_get_time_ms();

    // Check for timeout (reset state if too long between bytes)
    if (state.config.rx_timeout_ms > 0 &&
        state.rx_state != RX_STATE_IDLE &&
        (now - state.rx_last_byte_time) > state.config.rx_timeout_ms) {
        LOG_W(TAG, "RX timeout, resetting state machine");
        state.stats.timeout_errors++;
        notify_event(STM32_FRAMING_EVENT_TIMEOUT, NULL, 0);
        rx_reset_state();
    }

    state.rx_last_byte_time = now;

    switch (state.rx_state) {
        case RX_STATE_IDLE:
            if (byte == STM32_PACKET_START_MARKER) {
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
            if (state.rx_expected_length > STM32_UART_MAX_PACKET_SIZE - PACKET_OVERHEAD) {
                LOG_W(TAG, "Packet too large: %u bytes", state.rx_expected_length);
                state.stats.framing_errors++;
                rx_reset_state();
            } else if (state.rx_expected_length == 0) {
                // Zero-length packet, skip to CRC
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
            if (byte == STM32_PACKET_END_MARKER) {
                // Validate CRC
                uint16_t calculated_crc = crc16_ccitt(state.rx_buffer, state.rx_index);
                if (calculated_crc == state.rx_crc) {
                    // Valid packet received
                    state.stats.packets_received++;
                    LOG_D(TAG, "Packet received: %u bytes", state.rx_index);
                    notify_event(STM32_FRAMING_EVENT_PACKET_RECEIVED,
                                state.rx_buffer, state.rx_index);
                } else {
                    // CRC mismatch
                    LOG_W(TAG, "CRC error: expected 0x%04X, got 0x%04X",
                            state.rx_crc, calculated_crc);
                    state.stats.crc_errors++;
                    notify_event(STM32_FRAMING_EVENT_CRC_ERROR, NULL, 0);
                }
            } else {
                // Invalid end marker
                LOG_W(TAG, "Invalid end marker: 0x%02X", byte);
                state.stats.framing_errors++;
                notify_event(STM32_FRAMING_EVENT_RX_ERROR, NULL, 0);
            }
            rx_reset_state();
            break;
    }
}

/**
 * @brief Receive task - processes incoming bytes from UART buffer
 */
static void rx_task(void *arg)
{
    uint8_t rx_buffer[64];

    LOG_I(TAG, "RX task started");

    while (1) {
        int available = hal_uart_available((hal_uart_port_t)STM32_UART_PORT);

        if (available > 0) {
            int to_read = (available > (int)sizeof(rx_buffer)) ? (int)sizeof(rx_buffer) : available;
            int bytes_read = hal_uart_read((hal_uart_port_t)STM32_UART_PORT,
                                           rx_buffer, to_read, 0);

            for (int i = 0; i < bytes_read; i++) {
                rx_process_byte(rx_buffer[i]);
            }
        } else {
            os_delay_ms(1);
        }
    }
}

// ============================================================================
// Public API Implementation
// ============================================================================

stm32_framing_config_t stm32_framing_get_default_config(void)
{
    stm32_framing_config_t config = {
        .baud_rate = STM32_UART_BAUD_RATE,
        .use_flow_control = false,
        .rx_timeout_ms = 1000,
        .callback = NULL,
        .user_data = NULL
    };
    return config;
}

stm32_framing_status_t stm32_framing_init(const stm32_framing_config_t *config)
{
    if (state.initialized) {
        LOG_W(TAG, "Already initialized");
        return STM32_FRAMING_ERR_ALREADY_INIT;
    }

    if (config == NULL) {
        LOG_E(TAG, "Config is NULL");
        return STM32_FRAMING_ERR_INVALID_PARAM;
    }

    LOG_I(TAG, "Initializing STM32 UART driver (baud=%lu)",
             (unsigned long)config->baud_rate);

    // Copy configuration
    memcpy(&state.config, config, sizeof(stm32_framing_config_t));

    // Initialize UART HAL
    hal_uart_config_t uart_config = hal_uart_get_default_config();
    uart_config.baud_rate = config->baud_rate;
    uart_config.tx_pin = STM32_UART_TX_PIN;
    uart_config.rx_pin = STM32_UART_RX_PIN;
    uart_config.rx_buffer_size = STM32_UART_RX_BUFFER_SIZE;
    uart_config.tx_buffer_size = STM32_UART_TX_BUFFER_SIZE;

    if (config->use_flow_control) {
        uart_config.flow_ctrl = HAL_UART_FLOW_CTRL_RTS_CTS;
        uart_config.rts_pin = STM32_UART_RTS_PIN;
        uart_config.cts_pin = STM32_UART_CTS_PIN;
    }

    if (!hal_uart_init((hal_uart_port_t)STM32_UART_PORT, &uart_config)) {
        LOG_E(TAG, "Failed to initialize UART HAL");
        return STM32_FRAMING_ERR_TX_FAILED;
    }

    // Create TX mutex
    state.tx_mutex = os_mutex_create();
    if (state.tx_mutex == NULL) {
        LOG_E(TAG, "Failed to create TX mutex");
        hal_uart_deinit((hal_uart_port_t)STM32_UART_PORT);
        return STM32_FRAMING_ERR_MEMORY;
    }

    // Reset state machine
    rx_reset_state();
    memset(&state.stats, 0, sizeof(stm32_framing_stats_t));

    // Create receive task
    os_result_t ret = os_task_create_pinned(rx_task, "stm32_rx", RX_TASK_STACK_SIZE,
                                           NULL, RX_TASK_PRIORITY, &state.rx_task_handle, 1);
    if (ret != OS_SUCCESS) {
        LOG_E(TAG, "Failed to create RX task");
        os_mutex_delete(state.tx_mutex);
        hal_uart_deinit((hal_uart_port_t)STM32_UART_PORT);
        return STM32_FRAMING_ERR_MEMORY;
    }

    state.initialized = true;
    LOG_I(TAG, "STM32 UART driver initialized");
    return STM32_FRAMING_OK;
}

stm32_framing_status_t stm32_framing_deinit(void)
{
    if (!state.initialized) {
        return STM32_FRAMING_OK;
    }

    LOG_I(TAG, "Deinitializing STM32 UART driver");

    // Stop RX task
    if (state.rx_task_handle != NULL) {
        os_task_delete(state.rx_task_handle);
        state.rx_task_handle = NULL;
    }

    // Delete TX mutex
    if (state.tx_mutex != NULL) {
        os_mutex_delete(state.tx_mutex);
        state.tx_mutex = NULL;
    }

    // Deinitialize UART HAL
    hal_uart_deinit((hal_uart_port_t)STM32_UART_PORT);

    state.initialized = false;
    LOG_I(TAG, "STM32 UART driver deinitialized");
    return STM32_FRAMING_OK;
}

stm32_framing_status_t stm32_framing_send_packet(const uint8_t *data, size_t length, uint32_t timeout_ms)
{
    if (!state.initialized) {
        return STM32_FRAMING_ERR_NOT_INITIALIZED;
    }

    if (length > STM32_UART_MAX_PACKET_SIZE - PACKET_OVERHEAD) {
        LOG_E(TAG, "Packet too large: %u bytes", length);
        return STM32_FRAMING_ERR_PACKET_TOO_LARGE;
    }

    // Acquire TX mutex
    if (os_mutex_take(state.tx_mutex, TX_MUTEX_TIMEOUT_MS) != OS_SUCCESS) {
        LOG_E(TAG, "Failed to acquire TX mutex");
        return STM32_FRAMING_ERR_TIMEOUT;
    }

    // Build packet: START + LENGTH(2) + DATA + CRC(2) + END
    uint8_t tx_buffer[STM32_UART_MAX_PACKET_SIZE];
    size_t tx_index = 0;

    // Start marker
    tx_buffer[tx_index++] = STM32_PACKET_START_MARKER;

    // Length (little-endian)
    tx_buffer[tx_index++] = (uint8_t)(length & 0xFF);
    tx_buffer[tx_index++] = (uint8_t)((length >> 8) & 0xFF);

    // Data
    if (data != NULL && length > 0) {
        memcpy(&tx_buffer[tx_index], data, length);
        tx_index += length;
    }

    // CRC (calculated over data only)
    uint16_t crc = crc16_ccitt(data, length);
    tx_buffer[tx_index++] = (uint8_t)(crc & 0xFF);
    tx_buffer[tx_index++] = (uint8_t)((crc >> 8) & 0xFF);

    // End marker
    tx_buffer[tx_index++] = STM32_PACKET_END_MARKER;

    // Send packet
    int sent = hal_uart_write((hal_uart_port_t)STM32_UART_PORT,
                               tx_buffer, tx_index, (int)timeout_ms);

    os_mutex_give(state.tx_mutex);

    if (sent != (int)tx_index) {
        LOG_E(TAG, "Failed to send packet: sent %d of %u bytes", sent, tx_index);
        return STM32_FRAMING_ERR_TX_FAILED;
    }

    state.stats.packets_sent++;
    LOG_D(TAG, "Packet sent: %u bytes (total frame: %u)", length, tx_index);

    // Notify TX complete
    notify_event(STM32_FRAMING_EVENT_TX_COMPLETE, NULL, length);

    return STM32_FRAMING_OK;
}

int stm32_framing_send_raw(const uint8_t *data, size_t length, uint32_t timeout_ms)
{
    if (!state.initialized) {
        return -1;
    }

    if (os_mutex_take(state.tx_mutex, TX_MUTEX_TIMEOUT_MS) != OS_SUCCESS) {
        return -1;
    }

    int sent = hal_uart_write((hal_uart_port_t)STM32_UART_PORT, data, length, (int)timeout_ms);

    os_mutex_give(state.tx_mutex);

    return sent;
}

bool stm32_framing_is_initialized(void)
{
    return state.initialized;
}

stm32_framing_status_t stm32_framing_get_stats(stm32_framing_stats_t *stats)
{
    if (stats == NULL) {
        return STM32_FRAMING_ERR_INVALID_PARAM;
    }

    memcpy(stats, &state.stats, sizeof(stm32_framing_stats_t));
    return STM32_FRAMING_OK;
}

void stm32_framing_reset_stats(void)
{
    memset(&state.stats, 0, sizeof(stm32_framing_stats_t));
}

stm32_framing_status_t stm32_framing_flush_rx(void)
{
    if (!state.initialized) {
        return STM32_FRAMING_ERR_NOT_INITIALIZED;
    }

    rx_reset_state();
    hal_uart_flush_rx((hal_uart_port_t)STM32_UART_PORT);

    return STM32_FRAMING_OK;
}
