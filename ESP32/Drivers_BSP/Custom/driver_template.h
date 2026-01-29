/**
 * @file driver_template.h
 * @brief Template for creating device drivers in the Drivers_BSP layer
 * 
 * Copy this file and rename to <device_name>.h
 */

#ifndef DRIVER_TEMPLATE_H
#define DRIVER_TEMPLATE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Initialize device driver
 * @return true on success, false on failure
 */
bool driver_template_init(void);

/**
 * @brief Deinitialize device driver
 */
void driver_template_deinit(void);

/**
 * @brief Read data from device
 * @param data Buffer to store read data
 * @param len Number of bytes to read
 * @return true on success, false on failure
 */
bool driver_template_read(uint8_t *data, size_t len);

/**
 * @brief Write data to device
 * @param data Data to write
 * @param len Number of bytes to write
 * @return true on success, false on failure
 */
bool driver_template_write(const uint8_t *data, size_t len);

/**
 * @brief Perform device-specific operation
 * @return true on success, false on failure
 */
bool driver_template_do_something(void);

#endif // DRIVER_TEMPLATE_H
