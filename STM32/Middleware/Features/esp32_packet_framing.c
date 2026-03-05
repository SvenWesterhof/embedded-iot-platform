/**
 * @file esp32_packet_framing.c
 * @brief ESP32 packet framing layer for UART communication
 * 
 * Implements packet-based UART communication with:
 * - Packet framing (0xAA start, 0x55 end)
 * - CRC16-CCITT validation
 * - Background receive task
 * - Thread-safe transmission
 */

#include "esp32_packet_framing.h"
#include "crc16.h"
#include "portable_log.h"
#include "pinout.h"
#include "hal_uart.h"
#include "os_wrapper.h"
#include <string.h>

static const char *TAG = "ESP32_UART";

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
    stm32_uart_config_t config;

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
    os_semaphore_handle_t rx_data_sem;               // Signaled when RX data available (from HAL callback)

    // Async TX state
    uint8_t tx_buffer[STM32_UART_MAX_PACKET_SIZE];  // Static buffer for async TX
    size_t tx_pending_length;                        // Length of pending TX
    os_semaphore_handle_t tx_complete_sem;           // Signaled when TX completes
    volatile bool tx_in_progress;                    // TX DMA in progress flag

    // Statistics
    stm32_uart_stats_t stats;
} stm32_uart_state_t;

static stm32_uart_state_t state = {0};

// ============================================================================
// Forward Declarations
// ============================================================================

static void notify_event(stm32_uart_event_type_t type, uint8_t *data, size_t length);

// ============================================================================
// Internal Functions
// ============================================================================

/**
 * @brief UART HAL event callback - handles RX_DATA and TX_DONE events
 * @note This is called from uart_event_task (task context), NOT from ISR context
 */
static void uart_hal_event_callback(hal_uart_port_t port, hal_uart_event_t *event, void *user_data)
{
    (void)port;
    (void)user_data;

    if (event->type == HAL_UART_EVENT_RX_DATA) {
        // RX data available - signal the rx_task
        // NOTE: This is called from task context, so use normal semaphore give (not _from_isr)
        if (state.rx_data_sem != NULL) {
            os_semaphore_give(state.rx_data_sem);
        }
    }
    else if (event->type == HAL_UART_EVENT_TX_DONE) {
        // TX DMA completed - signal the semaphore
        state.tx_in_progress = false;
        if (state.tx_complete_sem != NULL) {
            os_semaphore_give(state.tx_complete_sem);
        }
        state.stats.packets_sent++;
        notify_event(STM32_UART_EVENT_TX_COMPLETE, NULL, state.tx_pending_length);
    }
}

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
static void notify_event(stm32_uart_event_type_t type, uint8_t *data, size_t length)
{
    if (state.config.callback != NULL) {
        stm32_uart_event_t event = {
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
        notify_event(STM32_UART_EVENT_TIMEOUT, NULL, 0);
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
                    notify_event(STM32_UART_EVENT_PACKET_RECEIVED, 
                                state.rx_buffer, state.rx_index);
                } else {
                    // CRC mismatch
                    LOG_W(TAG, "CRC error: expected 0x%04X, got 0x%04X", 
                            state.rx_crc, calculated_crc);
                    state.stats.crc_errors++;
                    notify_event(STM32_UART_EVENT_CRC_ERROR, NULL, 0);
                }
            } else {
                // Invalid end marker
                LOG_W(TAG, "Invalid end marker: 0x%02X", byte);
                state.stats.framing_errors++;
                notify_event(STM32_UART_EVENT_RX_ERROR, NULL, 0);
            }
            rx_reset_state();
            break;
    }
}

/**
 * @brief Receive task - processes incoming bytes from UART buffer
 *
 * Blocks on rx_data_sem which is signaled by HAL callback when RX data arrives.
 * Uses ISR-based triggering for low latency and reduced CPU usage.
 */
static void rx_task(void *arg)
{
    (void)arg;
    uint8_t rx_buffer[64];

    LOG_I(TAG, "RX task started (ISR-based)");

    while (1) {
        // Block until RX data available (signaled by HAL callback) or timeout
        // Use 50ms timeout as safety fallback
        os_semaphore_take(state.rx_data_sem, 50);

        // Process all available data
        int available = hal_uart_available((hal_uart_port_t)STM32_UART_PORT);

        if (available > 0) {
            int to_read = (available > (int)sizeof(rx_buffer)) ? (int)sizeof(rx_buffer) : available;
            int bytes_read = hal_uart_read((hal_uart_port_t)STM32_UART_PORT,
                                           rx_buffer, to_read, 0);

            for (int i = 0; i < bytes_read; i++) {
                rx_process_byte(rx_buffer[i]);
            }
        }
    }
}

// ============================================================================
// Public API Implementation
// ============================================================================

stm32_uart_config_t stm32_uart_get_default_config(void)
{
    stm32_uart_config_t config = {
        .baud_rate = STM32_UART_BAUD_RATE,
        .use_flow_control = false,
        .rx_timeout_ms = 1000,
        .callback = NULL,
        .user_data = NULL
    };
    return config;
}

uart_driver_status_t stm32_uart_init(const stm32_uart_config_t *config)
{
    if (state.initialized) {
        LOG_W(TAG, "Already initialized");
        return UART_DRV_ERR_ALREADY_INIT;
    }
    
    if (config == NULL) {
        LOG_E(TAG, "Config is NULL");
        return UART_DRV_ERR_INVALID_PARAM;
    }
    
    LOG_I(TAG, "Initializing STM32 UART driver (baud=%lu)", 
             (unsigned long)config->baud_rate);
    
    // Copy configuration
    memcpy(&state.config, config, sizeof(stm32_uart_config_t));
    
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
        return UART_DRV_ERR_TX_FAILED;
    }
    
    // Create TX mutex
    state.tx_mutex = os_mutex_create();
    if (state.tx_mutex == NULL) {
        LOG_E(TAG, "Failed to create TX mutex");
        hal_uart_deinit((hal_uart_port_t)STM32_UART_PORT);
        return UART_DRV_ERR_MEMORY;
    }

    // Create RX data semaphore (binary, initially empty)
    state.rx_data_sem = os_semaphore_create_binary();
    if (state.rx_data_sem == NULL) {
        LOG_E(TAG, "Failed to create RX semaphore");
        os_mutex_delete(state.tx_mutex);
        hal_uart_deinit((hal_uart_port_t)STM32_UART_PORT);
        return UART_DRV_ERR_MEMORY;
    }

    // Create TX complete semaphore (binary, initially available)
    state.tx_complete_sem = os_semaphore_create_binary();
    if (state.tx_complete_sem == NULL) {
        LOG_E(TAG, "Failed to create TX semaphore");
        os_semaphore_delete(state.rx_data_sem);
        os_mutex_delete(state.tx_mutex);
        hal_uart_deinit((hal_uart_port_t)STM32_UART_PORT);
        return UART_DRV_ERR_MEMORY;
    }
    // Give semaphore initially so first TX can proceed
    os_semaphore_give(state.tx_complete_sem);
    state.tx_in_progress = false;

    // Register callback for TX_DONE events
    hal_uart_register_callback((hal_uart_port_t)STM32_UART_PORT, uart_hal_event_callback, NULL);

    // Reset state machine
    rx_reset_state();
    memset(&state.stats, 0, sizeof(stm32_uart_stats_t));
    
    // Create receive task
    os_result_t ret = os_task_create_pinned(rx_task, "stm32_rx", RX_TASK_STACK_SIZE,
                                           NULL, RX_TASK_PRIORITY, &state.rx_task_handle, 1);
    if (ret != OS_SUCCESS) {
        LOG_E(TAG, "Failed to create RX task");
        os_semaphore_delete(state.tx_complete_sem);
        os_semaphore_delete(state.rx_data_sem);
        os_mutex_delete(state.tx_mutex);
        hal_uart_deinit((hal_uart_port_t)STM32_UART_PORT);
        return UART_DRV_ERR_MEMORY;
    }
    
    state.initialized = true;
    LOG_I(TAG, "STM32 UART driver initialized");
    return UART_DRV_OK;
}

uart_driver_status_t stm32_uart_deinit(void)
{
    if (!state.initialized) {
        return UART_DRV_OK;
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

    // Delete RX data semaphore
    if (state.rx_data_sem != NULL) {
        os_semaphore_delete(state.rx_data_sem);
        state.rx_data_sem = NULL;
    }

    // Delete TX semaphore
    if (state.tx_complete_sem != NULL) {
        os_semaphore_delete(state.tx_complete_sem);
        state.tx_complete_sem = NULL;
    }

    // Unregister callback and deinitialize UART HAL
    hal_uart_unregister_callback((hal_uart_port_t)STM32_UART_PORT);
    hal_uart_deinit((hal_uart_port_t)STM32_UART_PORT);
    
    state.initialized = false;
    LOG_I(TAG, "STM32 UART driver deinitialized");
    return UART_DRV_OK;
}

uart_driver_status_t stm32_uart_send_packet(const uint8_t *data, size_t length, uint32_t timeout_ms)
{
    if (!state.initialized) {
        return UART_DRV_ERR_NOT_INITIALIZED;
    }
    
    if (length > STM32_UART_MAX_PACKET_SIZE - PACKET_OVERHEAD) {
        LOG_E(TAG, "Packet too large: %u bytes", (unsigned)length);
        return UART_DRV_ERR_PACKET_TOO_LARGE;
    }

    // Acquire TX mutex
    if (os_mutex_take(state.tx_mutex, TX_MUTEX_TIMEOUT_MS) != OS_SUCCESS) {
        LOG_E(TAG, "Failed to acquire TX mutex");
        return UART_DRV_ERR_TIMEOUT;
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
        LOG_E(TAG, "Failed to send packet: sent %d of %u bytes", sent, (unsigned)tx_index);
        return UART_DRV_ERR_TX_FAILED;
    }
    
    state.stats.packets_sent++;
    LOG_D(TAG, "Packet sent: %u bytes (total frame: %u)", length, tx_index);

    // Notify TX complete
    notify_event(STM32_UART_EVENT_TX_COMPLETE, NULL, length);

    return UART_DRV_OK;
}

uart_driver_status_t stm32_uart_send_packet_async(const uint8_t *data, size_t length)
{
    if (!state.initialized) {
        return UART_DRV_ERR_NOT_INITIALIZED;
    }

    if (length > STM32_UART_MAX_PACKET_SIZE - PACKET_OVERHEAD) {
        LOG_E(TAG, "Packet too large: %u bytes", (unsigned)length);
        return UART_DRV_ERR_PACKET_TOO_LARGE;
    }

    // Acquire TX mutex to protect buffer access
    if (os_mutex_take(state.tx_mutex, TX_MUTEX_TIMEOUT_MS) != OS_SUCCESS) {
        LOG_E(TAG, "Failed to acquire TX mutex");
        return UART_DRV_ERR_TIMEOUT;
    }

    // Wait for previous TX to complete (with timeout)
    if (state.tx_in_progress) {
        if (os_semaphore_take(state.tx_complete_sem, TX_MUTEX_TIMEOUT_MS) != OS_SUCCESS) {
            os_mutex_give(state.tx_mutex);
            LOG_E(TAG, "Previous TX did not complete in time");
            return UART_DRV_ERR_TIMEOUT;
        }
    }

    // Also wait for HAL layer TX to complete
    uint32_t wait_start = os_get_time_ms();
    while (hal_uart_tx_busy((hal_uart_port_t)STM32_UART_PORT)) {
        if ((os_get_time_ms() - wait_start) > TX_MUTEX_TIMEOUT_MS) {
            os_mutex_give(state.tx_mutex);
            LOG_E(TAG, "HAL TX did not complete in time");
            return UART_DRV_ERR_TIMEOUT;
        }
        os_delay_ms(1);
    }

    // Build packet in the static TX buffer
    size_t tx_index = 0;

    // Start marker
    state.tx_buffer[tx_index++] = STM32_PACKET_START_MARKER;

    // Length (little-endian)
    state.tx_buffer[tx_index++] = (uint8_t)(length & 0xFF);
    state.tx_buffer[tx_index++] = (uint8_t)((length >> 8) & 0xFF);

    // Data
    if (data != NULL && length > 0) {
        memcpy(&state.tx_buffer[tx_index], data, length);
        tx_index += length;
    }

    // CRC (calculated over data only)
    uint16_t crc = crc16_ccitt(data, length);
    state.tx_buffer[tx_index++] = (uint8_t)(crc & 0xFF);
    state.tx_buffer[tx_index++] = (uint8_t)((crc >> 8) & 0xFF);

    // End marker
    state.tx_buffer[tx_index++] = STM32_PACKET_END_MARKER;

    // Store pending length for callback
    state.tx_pending_length = length;
    state.tx_in_progress = true;

    // Start async DMA transmission
    if (!hal_uart_write_async((hal_uart_port_t)STM32_UART_PORT, state.tx_buffer, tx_index)) {
        state.tx_in_progress = false;
        os_mutex_give(state.tx_mutex);
        LOG_E(TAG, "Failed to start async TX");
        return UART_DRV_ERR_TX_FAILED;
    }

    os_mutex_give(state.tx_mutex);

    LOG_D(TAG, "Async packet TX started: %u bytes (total frame: %u)", length, tx_index);
    return UART_DRV_OK;
}

bool stm32_uart_tx_busy(void)
{
    return state.tx_in_progress;
}

uart_driver_status_t stm32_uart_wait_tx_complete(uint32_t timeout_ms)
{
    if (!state.initialized) {
        return UART_DRV_ERR_NOT_INITIALIZED;
    }

    if (!state.tx_in_progress) {
        return UART_DRV_OK;
    }

    if (os_semaphore_take(state.tx_complete_sem, timeout_ms) != OS_SUCCESS) {
        return UART_DRV_ERR_TIMEOUT;
    }

    // Give it back since we just wanted to wait, not consume it
    os_semaphore_give(state.tx_complete_sem);
    return UART_DRV_OK;
}

int stm32_uart_send_raw(const uint8_t *data, size_t length, uint32_t timeout_ms)
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

bool stm32_uart_is_initialized(void)
{
    return state.initialized;
}

uart_driver_status_t stm32_uart_get_stats(stm32_uart_stats_t *stats)
{
    if (stats == NULL) {
        return UART_DRV_ERR_INVALID_PARAM;
    }
    
    memcpy(stats, &state.stats, sizeof(stm32_uart_stats_t));
    return UART_DRV_OK;
}

void stm32_uart_reset_stats(void)
{
    memset(&state.stats, 0, sizeof(stm32_uart_stats_t));
}

uart_driver_status_t stm32_uart_flush_rx(void)
{
    if (!state.initialized) {
        return UART_DRV_ERR_NOT_INITIALIZED;
    }
    
    rx_reset_state();
    hal_uart_flush_rx((hal_uart_port_t)STM32_UART_PORT);
    
    return UART_DRV_OK;
}
