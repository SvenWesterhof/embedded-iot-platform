#ifndef HAL_I2C_H
#define HAL_I2C_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Initialize I2C HAL
 * @return true if successful
 */
bool hal_i2c_init(void);

/**
 * @brief Write data to I2C device
 * @param addr I2C device address (7-bit)
 * @param data Data to write
 * @param len Length of data
 * @return true if successful
 */
bool hal_i2c_write(uint8_t addr, const uint8_t *data, size_t len);

/**
 * @brief Read data from I2C device
 * @param addr I2C device address (7-bit)
 * @param data Buffer to store read data
 * @param len Number of bytes to read
 * @return true if successful
 */
bool hal_i2c_read(uint8_t addr, uint8_t *data, size_t len);

/**
 * @brief Write then read from I2C device
 * @param addr I2C device address (7-bit)
 * @param write_data Data to write
 * @param write_len Length of write data
 * @param read_data Buffer to store read data
 * @param read_len Number of bytes to read
 * @return true if successful
 */
bool hal_i2c_write_read(uint8_t addr, const uint8_t *write_data, size_t write_len, 
                        uint8_t *read_data, size_t read_len);

#endif // HAL_I2C_H
