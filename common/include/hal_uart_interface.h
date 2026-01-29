/**
 * @file hal_uart_interface.h
 * @brief Hardware Abstraction Layer interface for UART
 *
 * Defines platform-independent UART interface using Strategy pattern.
 */

#ifndef HAL_UART_INTERFACE_H
#define HAL_UART_INTERFACE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// UART Types and Enums
// ============================================================================

/**
 * @brief UART port identifier
 */
typedef enum {
    HAL_UART_PORT_0 = 0,    /**< UART port 0 */
    HAL_UART_PORT_1 = 1,    /**< UART port 1 */
    HAL_UART_PORT_2 = 2,    /**< UART port 2 */
    HAL_UART_PORT_MAX
} hal_uart_port_t;

/**
 * @brief UART configuration structure
 */
typedef struct {
    uint32_t baud_rate;     /**< Baud rate (e.g., 115200) */
    uint8_t data_bits;      /**< Data bits (5, 6, 7, 8) */
    uint8_t stop_bits;      /**< Stop bits (1, 2) */
    uint8_t parity;         /**< Parity: 0=None, 1=Odd, 2=Even */
    bool flow_control;      /**< Hardware flow control (RTS/CTS) */
} hal_uart_config_t;

/**
 * @brief UART receive callback function type
 * @param port UART port that received data
 * @param data Pointer to received data
 * @param length Number of bytes received
 */
typedef void (*hal_uart_rx_callback_t)(hal_uart_port_t port, const uint8_t* data, size_t length);

// ============================================================================
// UART HAL Interface
// ============================================================================

/**
 * @brief UART Hardware Abstraction Layer interface
 */
typedef struct {
    /**
     * @brief Initialize UART HAL
     * @return true if successful
     */
    bool (*init)(void);

    /**
     * @brief Configure UART port
     * @param port UART port to configure
     * @param config Configuration parameters
     * @return true if successful
     */
    bool (*config)(hal_uart_port_t port, const hal_uart_config_t* config);

    /**
     * @brief Write data to UART (blocking)
     * @param port UART port
     * @param data Data to send
     * @param length Number of bytes to send
     * @param timeout_ms Timeout in milliseconds (0 = no timeout)
     * @return Number of bytes actually written
     */
    size_t (*write)(hal_uart_port_t port, const uint8_t* data, size_t length, uint32_t timeout_ms);

    /**
     * @brief Read data from UART (blocking)
     * @param port UART port
     * @param buffer Buffer to store received data
     * @param length Maximum number of bytes to read
     * @param timeout_ms Timeout in milliseconds (0 = no timeout)
     * @return Number of bytes actually read
     */
    size_t (*read)(hal_uart_port_t port, uint8_t* buffer, size_t length, uint32_t timeout_ms);

    /**
     * @brief Check if data is available to read
     * @param port UART port
     * @return Number of bytes available in receive buffer
     */
    size_t (*available)(hal_uart_port_t port);

    /**
     * @brief Flush transmit buffer
     * @param port UART port
     * @return true if successful
     */
    bool (*flush)(hal_uart_port_t port);

    /**
     * @brief Register receive callback (interrupt-driven mode)
     * @param port UART port
     * @param callback Callback function to call when data is received
     * @return true if successful
     */
    bool (*register_rx_callback)(hal_uart_port_t port, hal_uart_rx_callback_t callback);

} hal_uart_interface_t;

// ============================================================================
// Global HAL Instance
// ============================================================================

/**
 * @brief Global UART HAL interface pointer
 */
extern const hal_uart_interface_t* hal_uart;

// Use the interface directly: hal_uart->init(), hal_uart->write(), etc.

#ifdef __cplusplus
}
#endif

#endif // HAL_UART_INTERFACE_H
