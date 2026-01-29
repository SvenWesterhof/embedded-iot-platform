/**
 * @file hal_i2c_stm32.c
 * @brief STM32 implementation of I2C HAL interface
 */

#include "hal_i2c.h"
#include "hal_i2c_interface.h"
#include "stm32f7xx_hal.h"

// ============================================================================
// STM32 I2C Implementation (Interface Pattern)
// ============================================================================

static bool stm32_i2c_init(void)
{
    // I2C peripherals initialized by STM32CubeMX generated code
    return true;
}

static hal_i2c_status_t stm32_i2c_master_transmit(hal_i2c_handle_t handle, uint16_t dev_address,
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

static hal_i2c_status_t stm32_i2c_master_receive(hal_i2c_handle_t handle, uint16_t dev_address,
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

static hal_i2c_status_t stm32_i2c_mem_write(hal_i2c_handle_t handle, uint16_t dev_address,
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

static hal_i2c_status_t stm32_i2c_mem_read(hal_i2c_handle_t handle, uint16_t dev_address,
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

// ============================================================================
// STM32 I2C Interface Registration
// ============================================================================

static const hal_i2c_interface_t stm32_i2c_impl = {
    .init = stm32_i2c_init,
    .master_transmit = stm32_i2c_master_transmit,
    .master_receive = stm32_i2c_master_receive,
    .mem_write = stm32_i2c_mem_write,
    .mem_read = stm32_i2c_mem_read,
};

const hal_i2c_interface_t* hal_i2c = &stm32_i2c_impl;
