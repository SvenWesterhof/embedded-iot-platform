/**
 * @file hal_i2c_interface.h
 * @brief Hardware Abstraction Layer interface for I2C
 *
 * Defines platform-independent I2C interface using Strategy pattern.
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
 * @brief I2C bus identifier
 */
typedef enum {
    HAL_I2C_BUS_0 = 0,      /**< I2C bus 0 */
    HAL_I2C_BUS_1 = 1,      /**< I2C bus 1 */
    HAL_I2C_BUS_2 = 2,      /**< I2C bus 2 */
    HAL_I2C_BUS_MAX
} hal_i2c_bus_t;

/**
 * @brief I2C clock speed
 */
typedef enum {
    HAL_I2C_SPEED_STANDARD = 100000,    /**< 100 kHz */
    HAL_I2C_SPEED_FAST = 400000,        /**< 400 kHz */
    HAL_I2C_SPEED_FAST_PLUS = 1000000,  /**< 1 MHz */
} hal_i2c_speed_t;

/**
 * @brief I2C configuration structure
 */
typedef struct {
    uint32_t clock_speed;   /**< Clock speed in Hz */
    uint8_t address_bits;   /**< 7 or 10 bit addressing */
    bool pullup_enable;     /**< Enable internal pull-ups (if available) */
} hal_i2c_config_t;

// ============================================================================
// I2C HAL Interface
// ============================================================================

/**
 * @brief I2C Hardware Abstraction Layer interface
 */
typedef struct {
    /**
     * @brief Initialize I2C HAL
     * @return true if successful
     */
    bool (*init)(void);

    /**
     * @brief Configure I2C bus
     * @param bus I2C bus to configure
     * @param config Configuration parameters
     * @return true if successful
     */
    bool (*config)(hal_i2c_bus_t bus, const hal_i2c_config_t* config);

    /**
     * @brief Write data to I2C device
     * @param bus I2C bus
     * @param device_addr 7-bit device address
     * @param data Data to write
     * @param length Number of bytes to write
     * @param timeout_ms Timeout in milliseconds
     * @return true if successful
     */
    bool (*write)(hal_i2c_bus_t bus, uint8_t device_addr, const uint8_t* data, size_t length, uint32_t timeout_ms);

    /**
     * @brief Read data from I2C device
     * @param bus I2C bus
     * @param device_addr 7-bit device address
     * @param buffer Buffer to store read data
     * @param length Number of bytes to read
     * @param timeout_ms Timeout in milliseconds
     * @return true if successful
     */
    bool (*read)(hal_i2c_bus_t bus, uint8_t device_addr, uint8_t* buffer, size_t length, uint32_t timeout_ms);

    /**
     * @brief Write to register then read (common pattern for I2C sensors)
     * @param bus I2C bus
     * @param device_addr 7-bit device address
     * @param reg_addr Register address to read from
     * @param buffer Buffer to store read data
     * @param length Number of bytes to read
     * @param timeout_ms Timeout in milliseconds
     * @return true if successful
     */
    bool (*write_read)(hal_i2c_bus_t bus, uint8_t device_addr, uint8_t reg_addr,
                       uint8_t* buffer, size_t length, uint32_t timeout_ms);

    /**
     * @brief Scan I2C bus for devices
     * @param bus I2C bus to scan
     * @param found_devices Array to store found device addresses
     * @param max_devices Maximum number of devices to find
     * @return Number of devices found
     */
    uint8_t (*scan)(hal_i2c_bus_t bus, uint8_t* found_devices, uint8_t max_devices);

} hal_i2c_interface_t;

// ============================================================================
// Global HAL Instance
// ============================================================================

/**
 * @brief Global I2C HAL interface pointer
 */
extern const hal_i2c_interface_t* hal_i2c;

// ============================================================================
// Helper Macros
// ============================================================================

#define hal_i2c_init()                          hal_i2c->init()
#define hal_i2c_configure(bus, cfg)            hal_i2c->config(bus, cfg)
#define hal_i2c_write_bytes(bus, addr, data, len, timeout) \
                                                hal_i2c->write(bus, addr, data, len, timeout)
#define hal_i2c_read_bytes(bus, addr, buf, len, timeout) \
                                                hal_i2c->read(bus, addr, buf, len, timeout)

#ifdef __cplusplus
}
#endif

#endif // HAL_I2C_INTERFACE_H
