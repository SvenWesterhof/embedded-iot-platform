/**
 * @file hal_i2c_interface.h
 * @brief Hardware Abstraction Layer interface for I2C
 *
 * Defines platform-independent I2C interface using Strategy pattern.
 * Aligns with existing hal_i2c API for minimal migration impact.
 */

#ifndef HAL_I2C_INTERFACE_H
#define HAL_I2C_INTERFACE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// I2C Types and Enums
// ============================================================================

/**
 * @brief I2C handle type (platform-independent)
 */
typedef void* hal_i2c_handle_t;

/**
 * @brief I2C status codes
 */
typedef enum {
    HAL_I2C_OK = 0,      /**< Operation successful */
    HAL_I2C_ERROR,       /**< Generic error */
    HAL_I2C_BUSY,        /**< Bus is busy */
    HAL_I2C_TIMEOUT      /**< Operation timed out */
} hal_i2c_status_t;

// ============================================================================
// I2C HAL Interface
// ============================================================================

/**
 * @brief I2C Hardware Abstraction Layer interface
 *
 * This interface matches the existing hal_i2c API for easy migration.
 * Usage: hal_i2c->master_transmit(handle, addr, data, size, timeout)
 */
typedef struct {
    /**
     * @brief Initialize I2C HAL
     * @return true if successful
     */
    bool (*init)(void);

    /**
     * @brief Transmit data to I2C device (master mode)
     * @param handle I2C handle
     * @param dev_address Device address (7-bit, shifted left by 1)
     * @param data Pointer to data buffer
     * @param size Number of bytes to transmit
     * @param timeout_ms Timeout in milliseconds
     * @return HAL_I2C_OK if successful
     */
    hal_i2c_status_t (*master_transmit)(hal_i2c_handle_t handle, uint16_t dev_address,
                                        uint8_t* data, uint16_t size, uint32_t timeout_ms);

    /**
     * @brief Receive data from I2C device (master mode)
     * @param handle I2C handle
     * @param dev_address Device address (7-bit, shifted left by 1)
     * @param data Pointer to data buffer
     * @param size Number of bytes to receive
     * @param timeout_ms Timeout in milliseconds
     * @return HAL_I2C_OK if successful
     */
    hal_i2c_status_t (*master_receive)(hal_i2c_handle_t handle, uint16_t dev_address,
                                       uint8_t* data, uint16_t size, uint32_t timeout_ms);

    /**
     * @brief Write to I2C device memory/register
     * @param handle I2C handle
     * @param dev_address Device address (7-bit, shifted left by 1)
     * @param mem_address Memory/register address
     * @param data Pointer to data buffer
     * @param size Number of bytes to write
     * @param timeout_ms Timeout in milliseconds
     * @return HAL_I2C_OK if successful
     */
    hal_i2c_status_t (*mem_write)(hal_i2c_handle_t handle, uint16_t dev_address,
                                  uint16_t mem_address, uint8_t* data, uint16_t size, uint32_t timeout_ms);

    /**
     * @brief Read from I2C device memory/register
     * @param handle I2C handle
     * @param dev_address Device address (7-bit, shifted left by 1)
     * @param mem_address Memory/register address
     * @param data Pointer to data buffer
     * @param size Number of bytes to read
     * @param timeout_ms Timeout in milliseconds
     * @return HAL_I2C_OK if successful
     */
    hal_i2c_status_t (*mem_read)(hal_i2c_handle_t handle, uint16_t dev_address,
                                 uint16_t mem_address, uint8_t* data, uint16_t size, uint32_t timeout_ms);

} hal_i2c_interface_t;

// ============================================================================
// Global HAL Instance
// ============================================================================

/**
 * @brief Global I2C HAL interface pointer
 *
 * Usage: hal_i2c->master_transmit(handle, addr, data, size, timeout)
 */
extern const hal_i2c_interface_t* hal_i2c;

#ifdef __cplusplus
}
#endif

#endif // HAL_I2C_INTERFACE_H
