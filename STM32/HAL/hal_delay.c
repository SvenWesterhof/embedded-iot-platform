#include "hal_delay.h"

// STM32-specific implementation
#include "stm32f7xx_hal.h"

/**
 * @brief Delay for specified milliseconds
 * @param milliseconds Number of milliseconds to delay
 */
void hal_delay_ms(uint32_t milliseconds)
{
    HAL_Delay(milliseconds);
}

/**
 * @brief Get current system tick count
 * @return Current tick value in milliseconds
 */
uint32_t hal_get_tick(void)
{
    return HAL_GetTick();
}
