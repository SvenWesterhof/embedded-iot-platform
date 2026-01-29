#ifndef HAL_I2C_H
#define HAL_I2C_H

/**
 * @file hal_i2c.h
 * @brief Platform-independent I2C abstraction layer
 * 
 * This HAL provides a consistent I2C API that can be ported to different platforms.
 */

#include <stdint.h>
#include <stdbool.h>

// I2C Handle Type (platform-independent)
typedef void* hal_i2c_handle_t;

// I2C Status
typedef enum {
    HAL_I2C_OK = 0,
    HAL_I2C_ERROR,
    HAL_I2C_BUSY,
    HAL_I2C_TIMEOUT
} hal_i2c_status_t;

// I2C Functions
hal_i2c_status_t hal_i2c_master_transmit(hal_i2c_handle_t handle, uint16_t dev_address, 
                                          uint8_t* data, uint16_t size, uint32_t timeout_ms);
hal_i2c_status_t hal_i2c_master_receive(hal_i2c_handle_t handle, uint16_t dev_address,
                                         uint8_t* data, uint16_t size, uint32_t timeout_ms);
hal_i2c_status_t hal_i2c_mem_write(hal_i2c_handle_t handle, uint16_t dev_address,
                                    uint16_t mem_address, uint8_t* data, uint16_t size, uint32_t timeout_ms);
hal_i2c_status_t hal_i2c_mem_read(hal_i2c_handle_t handle, uint16_t dev_address,
                                   uint16_t mem_address, uint8_t* data, uint16_t size, uint32_t timeout_ms);

#endif // HAL_I2C_H
