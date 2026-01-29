/**
 * @file hal_uart.h
 * @brief UART Hardware Abstraction Layer
 *
 * Provides a portable UART interface abstracting STM32 HAL specifics.
 * Supports hardware flow control (RTS/CTS), configurable baud rate,
 * and event-driven reception.
 */

#ifndef HAL_UART_H
#define HAL_UART_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

// Forward declaration to avoid circular dependency with stm32f7xx_hal.h
struct __UART_HandleTypeDef;

/**
 * @brief UART port identifier
 */
typedef enum {
    HAL_UART_PORT_0 = 0,
    HAL_UART_PORT_1 = 1,
    HAL_UART_PORT_2 = 2,
    HAL_UART_PORT_MAX
} hal_uart_port_t;

/**
 * @brief UART parity configuration
 */
typedef enum {
    HAL_UART_PARITY_NONE = 0,
    HAL_UART_PARITY_EVEN,
    HAL_UART_PARITY_ODD
} hal_uart_parity_t;

/**
 * @brief UART stop bits configuration
 */
typedef enum {
    HAL_UART_STOP_BITS_1 = 1,
    HAL_UART_STOP_BITS_1_5,
    HAL_UART_STOP_BITS_2
} hal_uart_stop_bits_t;

/**
 * @brief UART flow control configuration
 */
typedef enum {
    HAL_UART_FLOW_CTRL_NONE = 0,
    HAL_UART_FLOW_CTRL_RTS,
    HAL_UART_FLOW_CTRL_CTS,
    HAL_UART_FLOW_CTRL_RTS_CTS
} hal_uart_flow_ctrl_t;

/**
 * @brief UART event types for callback notifications
 */
typedef enum {
    HAL_UART_EVENT_RX_DATA,         /**< Data received */
    HAL_UART_EVENT_TX_DONE,         /**< Transmission complete */
    HAL_UART_EVENT_RX_OVERFLOW,     /**< RX buffer overflow */
    HAL_UART_EVENT_FRAME_ERROR,     /**< Framing error */
    HAL_UART_EVENT_PARITY_ERROR,    /**< Parity error */
    HAL_UART_EVENT_BREAK            /**< Break condition detected */
} hal_uart_event_type_t;

/**
 * @brief UART event data structure
 */
typedef struct {
    hal_uart_event_type_t type;     /**< Event type */
    size_t size;                    /**< Data size (for RX_DATA event) */
} hal_uart_event_t;

/**
 * @brief UART configuration structure
 */
typedef struct {
    uint32_t baud_rate;             /**< Baud rate (e.g., 115200, 921600) */
    uint8_t data_bits;              /**< Data bits (5, 6, 7, 8) */
    hal_uart_parity_t parity;       /**< Parity configuration */
    hal_uart_stop_bits_t stop_bits; /**< Stop bits configuration */
    hal_uart_flow_ctrl_t flow_ctrl; /**< Flow control configuration */
    int8_t tx_pin;                  /**< TX pin number (-1 for default) */
    int8_t rx_pin;                  /**< RX pin number (-1 for default) */
    int8_t rts_pin;                 /**< RTS pin number (-1 if not used) */
    int8_t cts_pin;                 /**< CTS pin number (-1 if not used) */
    size_t rx_buffer_size;          /**< RX ring buffer size */
    size_t tx_buffer_size;          /**< TX ring buffer size (0 for blocking) */
} hal_uart_config_t;

/**
 * @brief UART event callback function type
 * @param port UART port that generated the event
 * @param event Event data
 * @param user_data User-provided context
 */
typedef void (*hal_uart_event_callback_t)(hal_uart_port_t port,
                                           hal_uart_event_t *event,
                                           void *user_data);

/**
 * @brief Initialize UART HAL for a specific port
 * @param port UART port to initialize
 * @param config UART configuration
 * @return true if successful, false otherwise
 */
bool hal_uart_init(hal_uart_port_t port, const hal_uart_config_t *config);

/**
 * @brief Deinitialize UART HAL for a specific port
 * @param port UART port to deinitialize
 * @return true if successful, false otherwise
 */
bool hal_uart_deinit(hal_uart_port_t port);

/**
 * @brief Write data to UART (blocking)
 * @param port UART port
 * @param data Data buffer to send
 * @param len Number of bytes to send
 * @param timeout_ms Timeout in milliseconds (0 for no wait, -1 for infinite)
 * @return Number of bytes written, or -1 on error
 *
 * @note BLOCKING: This function waits until all data is transmitted or timeout occurs.
 *       For non-blocking transmission, use hal_uart_write_async().
 */
int hal_uart_write(hal_uart_port_t port, const uint8_t *data, size_t len, int timeout_ms);

/**
 * @brief Write data to UART asynchronously using DMA (non-blocking)
 * @param port UART port
 * @param data Data buffer to send (must remain valid until transmission completes)
 * @param len Number of bytes to send
 * @return true if transmission started successfully, false on error
 *
 * @note NON-BLOCKING: Returns immediately after starting DMA transmission.
 * @warning The data buffer must remain valid until HAL_UART_EVENT_TX_DONE event
 *          is received via the registered callback.
 * @note Requires TX DMA to be configured in CubeMX (Normal mode).
 * @note Only one async transmission can be active at a time per port.
 */
bool hal_uart_write_async(hal_uart_port_t port, const uint8_t *data, size_t len);

/**
 * @brief Read data from UART
 * @param port UART port
 * @param data Buffer to store received data
 * @param len Maximum number of bytes to read
 * @param timeout_ms Timeout in milliseconds (0 for no wait, -1 for infinite)
 * @return Number of bytes read, or -1 on error
 */
int hal_uart_read(hal_uart_port_t port, uint8_t *data, size_t len, int timeout_ms);

/**
 * @brief Get number of bytes available in RX buffer
 * @param port UART port
 * @return Number of bytes available, or -1 on error
 */
int hal_uart_available(hal_uart_port_t port);

/**
 * @brief Check if async TX is in progress
 * @param port UART port
 * @return true if TX is in progress, false otherwise
 */
bool hal_uart_tx_busy(hal_uart_port_t port);

/**
 * @brief Flush TX buffer (wait for all data to be sent)
 * @param port UART port
 * @param timeout_ms Timeout in milliseconds
 * @return true if successful, false on timeout or error
 */
bool hal_uart_flush_tx(hal_uart_port_t port, int timeout_ms);

/**
 * @brief Flush RX buffer (discard all received data)
 * @param port UART port
 * @return true if successful, false otherwise
 */
bool hal_uart_flush_rx(hal_uart_port_t port);

/**
 * @brief Register event callback for UART events
 * @param port UART port
 * @param callback Callback function
 * @param user_data User-provided context passed to callback
 * @return true if successful, false otherwise
 */
bool hal_uart_register_callback(hal_uart_port_t port,
                                 hal_uart_event_callback_t callback,
                                 void *user_data);

/**
 * @brief Unregister event callback
 * @param port UART port
 * @return true if successful, false otherwise
 */
bool hal_uart_unregister_callback(hal_uart_port_t port);

/**
 * @brief Set UART baud rate at runtime
 * @param port UART port
 * @param baud_rate New baud rate
 * @return true if successful, false otherwise
 */
bool hal_uart_set_baudrate(hal_uart_port_t port, uint32_t baud_rate);

/**
 * @brief Get default UART configuration
 * @return Default configuration structure
 */
hal_uart_config_t hal_uart_get_default_config(void);

/**
 * @brief Handle UART IDLE interrupt (ISR context)
 * @param huart UART handle (void* to avoid circular dependency)
 *
 * @note This function must be called from the USART IRQ handler (e.g., USART2_IRQHandler)
 *       when the IDLE flag is detected. Add this to stm32f7xx_it.c:
 *
 * @code
 * void USART2_IRQHandler(void)
 * {
 *     hal_uart_idle_isr(&huart2);  // Handle IDLE interrupt
 *     HAL_UART_IRQHandler(&huart2); // Handle other interrupts
 * }
 * @endcode
 */
void hal_uart_idle_isr(void *huart);

/**
 * @brief Get debug ISR counters (for diagnostics)
 * @param idle_count Pointer to store IDLE interrupt count (can be NULL)
 * @param dma_ht_count Pointer to store DMA Half-Transfer interrupt count (can be NULL)
 * @param dma_tc_count Pointer to store DMA Transfer-Complete interrupt count (can be NULL)
 */
void hal_uart_get_isr_counters(uint32_t *idle_count, uint32_t *dma_ht_count, uint32_t *dma_tc_count);

#endif // HAL_UART_H
