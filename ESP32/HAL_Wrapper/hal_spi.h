#ifndef HAL_SPI_H
#define HAL_SPI_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Initialize SPI HAL
 * @return true if successful
 */
bool hal_spi_init(void);

/**
 * @brief Transmit data over SPI
 * @param data Data to transmit
 * @param len Length of data
 * @return true if successful
 */
bool hal_spi_transmit(const uint8_t *data, size_t len);

/**
 * @brief Receive data over SPI
 * @param data Buffer to store received data
 * @param len Number of bytes to receive
 * @return true if successful
 */
bool hal_spi_receive(uint8_t *data, size_t len);

/**
 * @brief Transmit and receive data over SPI
 * @param tx_data Data to transmit
 * @param rx_data Buffer to store received data
 * @param len Length of data
 * @return true if successful
 */
bool hal_spi_transfer(const uint8_t *tx_data, uint8_t *rx_data, size_t len);

#endif // HAL_SPI_H
