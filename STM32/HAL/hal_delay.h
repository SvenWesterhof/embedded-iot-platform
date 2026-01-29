#ifndef HAL_DELAY_H
#define HAL_DELAY_H

/**
 * @file hal_delay.h
 * @brief Platform-independent delay and timing functions
 */

#include <stdint.h>

// Timing Functions
void hal_delay_ms(uint32_t milliseconds);
uint32_t hal_get_tick(void);

#endif // HAL_DELAY_H
