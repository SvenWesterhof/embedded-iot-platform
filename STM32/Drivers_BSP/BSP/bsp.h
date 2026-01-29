#ifndef BSP_H
#define BSP_H

/**
 * @file bsp.h
 * @brief Board Support Package - Board-level initialization
 * 
 * This module handles board-level initialization including:
 * - GPIO configuration
 * - Clock setup
 * - Peripheral initialization
 * - Board-specific hardware setup
 */

#include "stm32f7xx_hal.h"
#include "pinout.h"
#include "hal_i2c.h"

// BSP Initialization
void BSP_Init(void);

// Board-level functions (if needed)
void BSP_LED_Init(void);
void BSP_LED_On(void);
void BSP_LED_Off(void);
void BSP_LED_Toggle(void);

// Peripheral access functions
hal_i2c_handle_t BSP_Get_TempSensor_I2C(void);
hal_i2c_handle_t BSP_Get_CurrentSensor_I2C(void);

#endif // BSP_H
