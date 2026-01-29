#ifndef OS_CONFIG_H
#define OS_CONFIG_H

/**
 * @file os_config.h
 * @brief Operating System Configuration
 * 
 * This file contains configuration for RTOS or bare-metal operation.
 */

// Operating Mode
#define USE_FREERTOS    0  // Set to 1 to enable FreeRTOS, 0 for bare-metal

// FreeRTOS Configuration (if enabled)
#if USE_FREERTOS

#define configUSE_PREEMPTION                    1
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configCPU_CLOCK_HZ                      216000000  // STM32F767 max freq
#define configTICK_RATE_HZ                      1000
#define configMAX_PRIORITIES                    5
#define configMINIMAL_STACK_SIZE                128
#define configTOTAL_HEAP_SIZE                   15360
#define configMAX_TASK_NAME_LEN                 16
#define configUSE_16_BIT_TICKS                  0
#define configIDLE_SHOULD_YIELD                 1

#endif // USE_FREERTOS

// Bare-Metal Configuration
#if !USE_FREERTOS

// Main loop timing
#define MAIN_LOOP_PERIOD_MS     10  // Main loop runs every 10ms

#endif

#endif // OS_CONFIG_H
