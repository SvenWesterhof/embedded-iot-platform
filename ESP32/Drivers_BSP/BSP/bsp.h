/**
 * @file bsp.h
 * @brief Board Support Package - Hardware initialization header
 * 
 * Provides board-level initialization and configuration for the
 * ESP32 Gateway project.
 */

#ifndef BSP_H
#define BSP_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Initialize board support package
 * 
 * Initializes all board-specific hardware:
 * - GPIO HAL
 * - UART for STM32 communication
 * - Handshake GPIO pins
 * - LED indicator
 * 
 * @return true if all initialization succeeded, false otherwise
 */
bool bsp_init(void);

/**
 * @brief Deinitialize board support package
 * 
 * Releases all hardware resources.
 * 
 * @return true if successful, false otherwise
 */
bool bsp_deinit(void);

/**
 * @brief Set STM32 wakeup pin state
 * 
 * Used to wake STM32 from low-power mode.
 * 
 * @param active true to assert wakeup signal, false to deassert
 */
void bsp_stm32_wakeup_set(bool active);

/**
 * @brief Check if STM32 is ready
 * 
 * Reads the STM32 ready GPIO pin to check handshake status.
 * 
 * @return true if STM32 is ready, false otherwise
 */
bool bsp_stm32_is_ready(void);

/**
 * @brief Set LED state
 * 
 * @param on true to turn LED on, false to turn off
 */
void bsp_led_set(bool on);

/**
 * @brief Toggle LED state
 */
void bsp_led_toggle(void);

#endif // BSP_H
