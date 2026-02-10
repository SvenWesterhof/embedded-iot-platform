/**
 * @file hal_delay_stm32.c
 * @brief STM32 implementation of Delay HAL interface
 */

#include "hal_delay.h"
#include "hal_delay_interface.h"
#include "stm32f7xx_hal.h"

// ============================================================================
// STM32 Delay Implementation (Interface Pattern)
// ============================================================================

static bool stm32_delay_init(void)
{
    // STM32 HAL tick timer initialized by SystemClock_Config()
    return true;
}

static void stm32_delay_ms(uint32_t ms)
{
    HAL_Delay(ms);
}

static void stm32_delay_us(uint32_t us)
{
    // Approximate microsecond delay using CPU cycles
    // Assumes 216 MHz clock (STM32F767)
    uint32_t cycles = us * (SystemCoreClock / 1000000U) / 4U;
    for (volatile uint32_t i = 0; i < cycles; i++) {
        __NOP();
    }
}

static uint32_t stm32_get_tick_ms(void)
{
    return HAL_GetTick();
}

static uint32_t stm32_get_tick_us(void)
{
    // Microsecond resolution using HAL tick and SysTick counter
    uint32_t ms = HAL_GetTick();
    uint32_t cycles = SysTick->LOAD - SysTick->VAL;
    uint32_t us_in_tick = (cycles * 1000) / (SystemCoreClock / 1000);
    return (ms * 1000) + us_in_tick;
}

static uint32_t stm32_elapsed_ms(uint32_t start_tick)
{
    uint32_t now = stm32_get_tick_ms();
    // Handle 32-bit wrap-around
    return (now >= start_tick) ? (now - start_tick) : (0xFFFFFFFF - start_tick + now + 1);
}

static bool stm32_is_timeout(uint32_t start_tick, uint32_t timeout_ms)
{
    return stm32_elapsed_ms(start_tick) >= timeout_ms;
}

// ============================================================================
// STM32 Delay Interface Registration
// ============================================================================

static const hal_delay_interface_t stm32_delay_impl = {
    .init = stm32_delay_init,
    .delay_ms = stm32_delay_ms,
    .delay_us = stm32_delay_us,
    .get_tick_ms = stm32_get_tick_ms,
    .get_tick_us = stm32_get_tick_us,
    .elapsed_ms = stm32_elapsed_ms,
    .is_timeout = stm32_is_timeout,
};

const hal_delay_interface_t* hal_delay = &stm32_delay_impl;
