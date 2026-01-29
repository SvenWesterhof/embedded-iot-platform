#include "hal_spi.h"

// STM32-specific implementation
#include "stm32f7xx_hal.h"
#include "stm32f7xx_hal_spi.h"

/**
 * @brief Transmit data over SPI
 */
hal_spi_status_t hal_spi_transmit(hal_spi_handle_t handle, uint8_t* data, uint16_t size, uint32_t timeout_ms)
{
    HAL_StatusTypeDef status = HAL_SPI_Transmit((SPI_HandleTypeDef*)handle, data, size, timeout_ms);
    
    switch (status) {
        case HAL_OK:      return HAL_SPI_OK;
        case HAL_BUSY:    return HAL_SPI_BUSY;
        case HAL_TIMEOUT: return HAL_SPI_TIMEOUT;
        default:          return HAL_SPI_ERROR;
    }
}

/**
 * @brief Receive data over SPI
 */
hal_spi_status_t hal_spi_receive(hal_spi_handle_t handle, uint8_t* data, uint16_t size, uint32_t timeout_ms)
{
    HAL_StatusTypeDef status = HAL_SPI_Receive((SPI_HandleTypeDef*)handle, data, size, timeout_ms);
    
    switch (status) {
        case HAL_OK:      return HAL_SPI_OK;
        case HAL_BUSY:    return HAL_SPI_BUSY;
        case HAL_TIMEOUT: return HAL_SPI_TIMEOUT;
        default:          return HAL_SPI_ERROR;
    }
}

/**
 * @brief Transmit and receive data over SPI simultaneously
 */
hal_spi_status_t hal_spi_transmit_receive(hal_spi_handle_t handle, uint8_t* tx_data, uint8_t* rx_data,
                                           uint16_t size, uint32_t timeout_ms)
{
    HAL_StatusTypeDef status = HAL_SPI_TransmitReceive((SPI_HandleTypeDef*)handle, tx_data, rx_data, size, timeout_ms);
    
    switch (status) {
        case HAL_OK:      return HAL_SPI_OK;
        case HAL_BUSY:    return HAL_SPI_BUSY;
        case HAL_TIMEOUT: return HAL_SPI_TIMEOUT;
        default:          return HAL_SPI_ERROR;
    }
}
