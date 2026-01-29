#ifndef HAL_SPI_H
#define HAL_SPI_H

/**
 * @file hal_spi.h
 * @brief Platform-independent SPI abstraction layer
 * 
 * This HAL provides a consistent SPI API that can be ported to different platforms.
 */

#include <stdint.h>
#include <stdbool.h>

// SPI Handle Type (platform-independent)
typedef void* hal_spi_handle_t;

// SPI Status
typedef enum {
    HAL_SPI_OK = 0,
    HAL_SPI_ERROR,
    HAL_SPI_BUSY,
    HAL_SPI_TIMEOUT
} hal_spi_status_t;

// SPI Functions
hal_spi_status_t hal_spi_transmit(hal_spi_handle_t handle, uint8_t* data, uint16_t size, uint32_t timeout_ms);
hal_spi_status_t hal_spi_receive(hal_spi_handle_t handle, uint8_t* data, uint16_t size, uint32_t timeout_ms);
hal_spi_status_t hal_spi_transmit_receive(hal_spi_handle_t handle, uint8_t* tx_data, uint8_t* rx_data, 
                                           uint16_t size, uint32_t timeout_ms);

#endif // HAL_SPI_H
