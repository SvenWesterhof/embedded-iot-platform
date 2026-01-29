#include "hal_i2c.h"

// STM32-specific implementation
#include "stm32f7xx_hal.h"
#include "stm32f7xx_hal_i2c.h"

/**
 * @brief Transmit data over I2C
 */
hal_i2c_status_t hal_i2c_master_transmit(hal_i2c_handle_t handle, uint16_t dev_address,
                                          uint8_t* data, uint16_t size, uint32_t timeout_ms)
{
    HAL_StatusTypeDef status = HAL_I2C_Master_Transmit((I2C_HandleTypeDef*)handle, 
                                                        dev_address, data, size, timeout_ms);
    
    switch (status) {
        case HAL_OK:      return HAL_I2C_OK;
        case HAL_BUSY:    return HAL_I2C_BUSY;
        case HAL_TIMEOUT: return HAL_I2C_TIMEOUT;
        default:          return HAL_I2C_ERROR;
    }
}

/**
 * @brief Receive data over I2C
 */
hal_i2c_status_t hal_i2c_master_receive(hal_i2c_handle_t handle, uint16_t dev_address,
                                         uint8_t* data, uint16_t size, uint32_t timeout_ms)
{
    HAL_StatusTypeDef status = HAL_I2C_Master_Receive((I2C_HandleTypeDef*)handle,
                                                       dev_address, data, size, timeout_ms);
    
    switch (status) {
        case HAL_OK:      return HAL_I2C_OK;
        case HAL_BUSY:    return HAL_I2C_BUSY;
        case HAL_TIMEOUT: return HAL_I2C_TIMEOUT;
        default:          return HAL_I2C_ERROR;
    }
}

/**
 * @brief Write to I2C device memory/register
 */
hal_i2c_status_t hal_i2c_mem_write(hal_i2c_handle_t handle, uint16_t dev_address,
                                    uint16_t mem_address, uint8_t* data, uint16_t size, uint32_t timeout_ms)
{
    HAL_StatusTypeDef status = HAL_I2C_Mem_Write((I2C_HandleTypeDef*)handle, dev_address,
                                                  mem_address, I2C_MEMADD_SIZE_8BIT, data, size, timeout_ms);
    
    switch (status) {
        case HAL_OK:      return HAL_I2C_OK;
        case HAL_BUSY:    return HAL_I2C_BUSY;
        case HAL_TIMEOUT: return HAL_I2C_TIMEOUT;
        default:          return HAL_I2C_ERROR;
    }
}

/**
 * @brief Read from I2C device memory/register
 */
hal_i2c_status_t hal_i2c_mem_read(hal_i2c_handle_t handle, uint16_t dev_address,
                                   uint16_t mem_address, uint8_t* data, uint16_t size, uint32_t timeout_ms)
{
    HAL_StatusTypeDef status = HAL_I2C_Mem_Read((I2C_HandleTypeDef*)handle, dev_address,
                                                 mem_address, I2C_MEMADD_SIZE_8BIT, data, size, timeout_ms);
    
    switch (status) {
        case HAL_OK:      return HAL_I2C_OK;
        case HAL_BUSY:    return HAL_I2C_BUSY;
        case HAL_TIMEOUT: return HAL_I2C_TIMEOUT;
        default:          return HAL_I2C_ERROR;
    }
}
