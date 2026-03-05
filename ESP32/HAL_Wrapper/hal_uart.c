/**
 * @file hal_uart.c
 * @brief UART Hardware Abstraction Layer Implementation
 * 
 * Implements UART abstraction using ESP-IDF UART driver.
 * Supports hardware flow control, DMA, and event-driven reception.
 */

#include "hal_uart.h"
#include <driver/uart.h>
#include "portable_log.h"
#include "os_wrapper.h"
#include <freertos/queue.h>  // Needed for uart_driver_install compatibility
#include <string.h>

static const char *TAG = "HAL_UART";

#define UART_EVENT_QUEUE_SIZE   20
#define UART_EVENT_TASK_STACK   2048
#define UART_EVENT_TASK_PRIO    12

/**
 * @brief Internal state for each UART port
 */
typedef struct {
    bool initialized;
    os_queue_handle_t event_queue;
    os_task_handle_t event_task;
    hal_uart_event_callback_t callback;
    void *user_data;
} hal_uart_state_t;

static hal_uart_state_t uart_state[HAL_UART_PORT_MAX] = {0};

/**
 * @brief Convert HAL port to ESP-IDF UART number
 */
static inline uart_port_t port_to_uart_num(hal_uart_port_t port)
{
    return (uart_port_t)port;
}

/**
 * @brief Convert HAL parity to ESP-IDF parity
 */
static uart_parity_t convert_parity(hal_uart_parity_t parity)
{
    switch (parity) {
        case HAL_UART_PARITY_EVEN: return UART_PARITY_EVEN;
        case HAL_UART_PARITY_ODD:  return UART_PARITY_ODD;
        default:                   return UART_PARITY_DISABLE;
    }
}

/**
 * @brief Convert HAL stop bits to ESP-IDF stop bits
 */
static uart_stop_bits_t convert_stop_bits(hal_uart_stop_bits_t stop_bits)
{
    switch (stop_bits) {
        case HAL_UART_STOP_BITS_1_5: return UART_STOP_BITS_1_5;
        case HAL_UART_STOP_BITS_2:   return UART_STOP_BITS_2;
        default:                      return UART_STOP_BITS_1;
    }
}

/**
 * @brief Convert HAL flow control to ESP-IDF flow control
 */
static uart_hw_flowcontrol_t convert_flow_ctrl(hal_uart_flow_ctrl_t flow_ctrl)
{
    switch (flow_ctrl) {
        case HAL_UART_FLOW_CTRL_RTS:     return UART_HW_FLOWCTRL_RTS;
        case HAL_UART_FLOW_CTRL_CTS:     return UART_HW_FLOWCTRL_CTS;
        case HAL_UART_FLOW_CTRL_RTS_CTS: return UART_HW_FLOWCTRL_CTS_RTS;
        default:                          return UART_HW_FLOWCTRL_DISABLE;
    }
}

/**
 * @brief Convert HAL data bits to ESP-IDF word length
 */
static uart_word_length_t convert_data_bits(uint8_t data_bits)
{
    switch (data_bits) {
        case 5: return UART_DATA_5_BITS;
        case 6: return UART_DATA_6_BITS;
        case 7: return UART_DATA_7_BITS;
        default: return UART_DATA_8_BITS;
    }
}

/**
 * @brief UART event processing task
 */
static void uart_event_task(void *arg)
{
    hal_uart_port_t port = (hal_uart_port_t)(uintptr_t)arg;
    uart_port_t uart_num = port_to_uart_num(port);
    uart_event_t event;
    hal_uart_event_t hal_event;

    while (1) {
        if (os_queue_receive(uart_state[port].event_queue, &event, OS_WAIT_FOREVER) == OS_SUCCESS) {
            if (uart_state[port].callback == NULL) {
                continue;
            }

            memset(&hal_event, 0, sizeof(hal_event));

            switch (event.type) {
                case UART_DATA:
                    hal_event.type = HAL_UART_EVENT_RX_DATA;
                    hal_event.size = event.size;
                    break;

                case UART_FIFO_OVF:
                case UART_BUFFER_FULL:
                    hal_event.type = HAL_UART_EVENT_RX_OVERFLOW;
                    uart_flush_input(uart_num);
                    os_queue_reset(uart_state[port].event_queue);
                    LOG_W(TAG, "UART%d RX overflow", port);
                    break;

                case UART_FRAME_ERR:
                    hal_event.type = HAL_UART_EVENT_FRAME_ERROR;
                    LOG_W(TAG, "UART%d frame error", port);
                    break;

                case UART_PARITY_ERR:
                    hal_event.type = HAL_UART_EVENT_PARITY_ERROR;
                    LOG_W(TAG, "UART%d parity error", port);
                    break;

                case UART_BREAK:
                    hal_event.type = HAL_UART_EVENT_BREAK;
                    break;

                default:
                    continue;
            }

            uart_state[port].callback(port, &hal_event, uart_state[port].user_data);
        }
    }
}

bool hal_uart_init(hal_uart_port_t port, const hal_uart_config_t *config)
{
    if (port >= HAL_UART_PORT_MAX || config == NULL) {
        LOG_E(TAG, "Invalid parameters");
        return false;
    }

    if (uart_state[port].initialized) {
        LOG_W(TAG, "UART%d already initialized", port);
        return false;
    }

    uart_port_t uart_num = port_to_uart_num(port);

    LOG_I(TAG, "Initializing UART%d (baud=%lu)", port, (unsigned long)config->baud_rate);

    // Configure UART parameters
    uart_config_t uart_config = {
        .baud_rate = (int)config->baud_rate,
        .data_bits = convert_data_bits(config->data_bits),
        .parity = convert_parity(config->parity),
        .stop_bits = convert_stop_bits(config->stop_bits),
        .flow_ctrl = convert_flow_ctrl(config->flow_ctrl),
        .rx_flow_ctrl_thresh = 122,  // RTS threshold
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_param_config(uart_num, &uart_config);
    if (err != ESP_OK) {
        LOG_E(TAG, "UART%d param config failed: %s", port, esp_err_to_name(err));
        return false;
    }

    // Set pins
    int tx_pin = (config->tx_pin >= 0) ? config->tx_pin : UART_PIN_NO_CHANGE;
    int rx_pin = (config->rx_pin >= 0) ? config->rx_pin : UART_PIN_NO_CHANGE;
    int rts_pin = (config->rts_pin >= 0) ? config->rts_pin : UART_PIN_NO_CHANGE;
    int cts_pin = (config->cts_pin >= 0) ? config->cts_pin : UART_PIN_NO_CHANGE;

    err = uart_set_pin(uart_num, tx_pin, rx_pin, rts_pin, cts_pin);
    if (err != ESP_OK) {
        LOG_E(TAG, "UART%d set pin failed: %s", port, esp_err_to_name(err));
        return false;
    }

    // Install UART driver with event queue
    size_t rx_buf_size = config->rx_buffer_size > 0 ? config->rx_buffer_size : 1024;
    size_t tx_buf_size = config->tx_buffer_size;

    err = uart_driver_install(uart_num, rx_buf_size, tx_buf_size,
                               UART_EVENT_QUEUE_SIZE, (QueueHandle_t*)&uart_state[port].event_queue, 0);
    if (err != ESP_OK) {
        LOG_E(TAG, "UART%d driver install failed: %s", port, esp_err_to_name(err));
        return false;
    }

    // Create event processing task
    char task_name[16];
    (void)snprintf(task_name, sizeof(task_name), "uart%d_evt", port);
    
    os_result_t ret = os_task_create_pinned(uart_event_task, task_name, 
                                  UART_EVENT_TASK_STACK, (void *)(uintptr_t)port,
                                  UART_EVENT_TASK_PRIO, &uart_state[port].event_task, 1);
    if (ret != OS_SUCCESS) {
        LOG_E(TAG, "UART%d event task create failed", port);
        uart_driver_delete(uart_num);
        return false;
    }

    uart_state[port].initialized = true;
    uart_state[port].callback = NULL;
    uart_state[port].user_data = NULL;

    LOG_I(TAG, "UART%d initialized successfully", port);
    return true;
}

bool hal_uart_deinit(hal_uart_port_t port)
{
    if (port >= HAL_UART_PORT_MAX) {
        return false;
    }

    if (!uart_state[port].initialized) {
        return true;
    }

    // Delete event task
    if (uart_state[port].event_task) {
        os_task_delete(uart_state[port].event_task);
        uart_state[port].event_task = NULL;
    }

    // Delete UART driver
    uart_port_t uart_num = port_to_uart_num(port);
    esp_err_t err = uart_driver_delete(uart_num);
    if (err != ESP_OK) {
        LOG_E(TAG, "UART%d driver delete failed: %s", port, esp_err_to_name(err));
        return false;
    }

    uart_state[port].initialized = false;
    uart_state[port].event_queue = NULL;
    uart_state[port].callback = NULL;
    uart_state[port].user_data = NULL;

    LOG_I(TAG, "UART%d deinitialized", port);
    return true;
}

int hal_uart_write(hal_uart_port_t port, const uint8_t *data, size_t len, int timeout_ms)
{
    if (port >= HAL_UART_PORT_MAX || !uart_state[port].initialized) {
        return -1;
    }

    if (data == NULL || len == 0) {
        return 0;
    }

    uart_port_t uart_num = port_to_uart_num(port);
    int written = uart_write_bytes(uart_num, data, len);

    if (written > 0 && timeout_ms != 0) {
        uint32_t ticks = (timeout_ms < 0) ? OS_WAIT_FOREVER : os_ms_to_ticks(timeout_ms);
        uart_wait_tx_done(uart_num, ticks);
    }

    return written;
}

int hal_uart_read(hal_uart_port_t port, uint8_t *data, size_t len, int timeout_ms)
{
    if (port >= HAL_UART_PORT_MAX || !uart_state[port].initialized) {
        return -1;
    }

    if (data == NULL || len == 0) {
        return 0;
    }

    uart_port_t uart_num = port_to_uart_num(port);
    uint32_t ticks = (timeout_ms < 0) ? OS_WAIT_FOREVER : os_ms_to_ticks(timeout_ms);

    return uart_read_bytes(uart_num, data, len, ticks);
}

int hal_uart_available(hal_uart_port_t port)
{
    if (port >= HAL_UART_PORT_MAX || !uart_state[port].initialized) {
        return -1;
    }

    size_t available = 0;
    uart_port_t uart_num = port_to_uart_num(port);
    
    esp_err_t err = uart_get_buffered_data_len(uart_num, &available);
    if (err != ESP_OK) {
        return -1;
    }

    return (int)available;
}

bool hal_uart_flush_tx(hal_uart_port_t port, int timeout_ms)
{
    if (port >= HAL_UART_PORT_MAX || !uart_state[port].initialized) {
        return false;
    }

    uart_port_t uart_num = port_to_uart_num(port);
    uint32_t ticks = (timeout_ms < 0) ? OS_WAIT_FOREVER : os_ms_to_ticks(timeout_ms);

    esp_err_t err = uart_wait_tx_done(uart_num, ticks);
    return (err == ESP_OK);
}

bool hal_uart_flush_rx(hal_uart_port_t port)
{
    if (port >= HAL_UART_PORT_MAX || !uart_state[port].initialized) {
        return false;
    }

    uart_port_t uart_num = port_to_uart_num(port);
    esp_err_t err = uart_flush_input(uart_num);
    
    // Also reset event queue to clear pending RX events
    if (uart_state[port].event_queue) {
        os_queue_reset(uart_state[port].event_queue);
    }

    return (err == ESP_OK);
}

bool hal_uart_register_callback(hal_uart_port_t port, 
                                 hal_uart_event_callback_t callback,
                                 void *user_data)
{
    if (port >= HAL_UART_PORT_MAX || !uart_state[port].initialized) {
        return false;
    }

    uart_state[port].callback = callback;
    uart_state[port].user_data = user_data;
    return true;
}

bool hal_uart_unregister_callback(hal_uart_port_t port)
{
    if (port >= HAL_UART_PORT_MAX) {
        return false;
    }

    uart_state[port].callback = NULL;
    uart_state[port].user_data = NULL;
    return true;
}

bool hal_uart_set_baudrate(hal_uart_port_t port, uint32_t baud_rate)
{
    if (port >= HAL_UART_PORT_MAX || !uart_state[port].initialized) {
        return false;
    }

    uart_port_t uart_num = port_to_uart_num(port);
    esp_err_t err = uart_set_baudrate(uart_num, baud_rate);

    if (err != ESP_OK) {
        LOG_E(TAG, "UART%d set baudrate failed: %s", port, esp_err_to_name(err));
        return false;
    }

    LOG_I(TAG, "UART%d baudrate set to %lu", port, (unsigned long)baud_rate);
    return true;
}

hal_uart_config_t hal_uart_get_default_config(void)
{
    hal_uart_config_t config = {
        .baud_rate = 115200,
        .data_bits = 8,
        .parity = HAL_UART_PARITY_NONE,
        .stop_bits = HAL_UART_STOP_BITS_1,
        .flow_ctrl = HAL_UART_FLOW_CTRL_NONE,
        .tx_pin = -1,
        .rx_pin = -1,
        .rts_pin = -1,
        .cts_pin = -1,
        .rx_buffer_size = 1024,
        .tx_buffer_size = 0,  // Blocking TX by default
    };
    return config;
}
