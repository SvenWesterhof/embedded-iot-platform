#ifndef HAL_DELAY_H
#define HAL_DELAY_H

#include <stdint.h>

/**
 * @brief Delay for specified milliseconds
 * @param ms Milliseconds to delay
 */
void hal_delay_ms(uint32_t ms);

/**
 * @brief Delay for specified microseconds
 * @param us Microseconds to delay
 */
void hal_delay_us(uint32_t us);

/**
 * @brief Get system tick count in milliseconds
 * @return Tick count
 */
uint32_t hal_get_tick_ms(void);

#endif // HAL_DELAY_H
