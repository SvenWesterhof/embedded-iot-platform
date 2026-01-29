/**
 * @file portable_log.h
 * @brief Portable logging abstraction for ESP32 and STM32
 * 
 * Provides unified logging API that works on both platforms:
 * - ESP32: Uses esp_log (colored output, log levels, timestamps)
 * - STM32: Uses printf, SEGGER RTT, or ITM (configurable)
 */

#ifndef PORTABLE_LOG_H
#define PORTABLE_LOG_H

#define STM32F7
#define USE_SEGGER_RTT

// ============================================================================
// Platform Detection
// ============================================================================

#if defined(ESP_PLATFORM) || defined(IDF_VER)
    #define PLATFORM_ESP32
#elif defined(STM32F0) || defined(STM32F1) || defined(STM32F2) || \
      defined(STM32F3) || defined(STM32F4) || defined(STM32F7) || \
      defined(STM32H7) || defined(STM32L0) || defined(STM32L1) || \
      defined(STM32L4) || defined(STM32L5) || defined(STM32G0) || \
      defined(STM32G4) || defined(STM32WB) || defined(STM32MP1)
    #define PLATFORM_STM32
    #include "stm32f7xx_hal.h"
#else
    #warning "Unknown platform - defaulting to generic logging"
    #define PLATFORM_GENERIC
#endif

// ============================================================================
// ESP32 Implementation (use esp_log directly)
// ============================================================================

#ifdef PLATFORM_ESP32
    #include <esp_log.h>
    
    #define LOG_I(tag, format, ...)  ESP_LOGI(tag, format, ##__VA_ARGS__)
    #define LOG_W(tag, format, ...)  ESP_LOGW(tag, format, ##__VA_ARGS__)
    #define LOG_E(tag, format, ...)  ESP_LOGE(tag, format, ##__VA_ARGS__)
    #define LOG_D(tag, format, ...)  ESP_LOGD(tag, format, ##__VA_ARGS__)
    #define LOG_V(tag, format, ...)  ESP_LOGV(tag, format, ##__VA_ARGS__)

// ============================================================================
// STM32 Implementation (configurable backend)
// ============================================================================

#elif defined(PLATFORM_STM32)
    #include <stdio.h>
    #include "core_cm7.h"  // For ITM peripheral access
    
    // Choose STM32 logging backend:
    // 1. PRINTF - Standard printf (requires retarget or semihosting)
    // 2. SEGGER_RTT - SEGGER Real-Time Transfer (J-Link)
    // 3. ITM - ARM Instrumentation Trace Macrocell (SWO)
    
    #if defined(USE_SEGGER_RTT)
        #include "SEGGER_RTT.h"
        #define LOG_OUTPUT(str) SEGGER_RTT_WriteString(0, str)
    #else
        // Default: Use ITM/SWO for STM32 (use CMSIS ITM_SendChar)
        static inline void LOG_ITM_SendString(const char *str) {
            while (*str) {
                ITM_SendChar((uint32_t)(*str++));
            }
        }
        #define LOG_OUTPUT(str) LOG_ITM_SendString(str)
    #endif
    
    // Log level colors (ANSI - works with most terminals)
    #ifdef LOG_USE_COLOR
        #define LOG_COLOR_RED     "\033[0;31m"
        #define LOG_COLOR_YELLOW  "\033[0;33m"
        #define LOG_COLOR_RESET   "\033[0m"
    #else
        #define LOG_COLOR_RED     ""
        #define LOG_COLOR_YELLOW  ""
        #define LOG_COLOR_RESET   ""
    #endif
    
    // Internal helper - Direct SEGGER_RTT_printf (zero stack allocation)
    // SEGGER_RTT_printf is thread-safe and uses internal buffers
    #if defined(USE_SEGGER_RTT)
        #define _LOG_PRINTF(level_char, color, tag, format, ...) \
            SEGGER_RTT_printf(0, color "%c (%lu) %s: " format LOG_COLOR_RESET "\n", \
                             level_char, (unsigned long)(HAL_GetTick()), tag, ##__VA_ARGS__)
    #else
        // Fallback: Use stack buffer for ITM (no printf support)
        #define _LOG_PRINTF(level_char, color, tag, format, ...) \
            do { \
                char _buf[128]; \
                snprintf(_buf, sizeof(_buf), color "%c (%lu) %s: " format LOG_COLOR_RESET "\n", \
                         level_char, (unsigned long)(HAL_GetTick()), tag, ##__VA_ARGS__); \
                LOG_OUTPUT(_buf); \
            } while(0)
    #endif

    // Log macros
    #define LOG_I(tag, format, ...)  _LOG_PRINTF('I', "", tag, format, ##__VA_ARGS__)
    #define LOG_W(tag, format, ...)  _LOG_PRINTF('W', LOG_COLOR_YELLOW, tag, format, ##__VA_ARGS__)
    #define LOG_E(tag, format, ...)  _LOG_PRINTF('E', LOG_COLOR_RED, tag, format, ##__VA_ARGS__)
    
    // Debug logs (can be compiled out for size optimization)
    #ifdef LOG_LEVEL_DEBUG
        #if defined(USE_SEGGER_RTT)
            #define LOG_D(tag, format, ...)  SEGGER_RTT_printf(0, "D (%lu) %s: " format "\n", \
                                                              (unsigned long)(HAL_GetTick()), tag, ##__VA_ARGS__)
            #define LOG_V(tag, format, ...)  SEGGER_RTT_printf(0, "V (%lu) %s: " format "\n", \
                                                              (unsigned long)(HAL_GetTick()), tag, ##__VA_ARGS__)
        #else
            #define LOG_D(tag, format, ...)  _LOG_PRINTF('D', "", tag, format, ##__VA_ARGS__)
            #define LOG_V(tag, format, ...)  _LOG_PRINTF('V', "", tag, format, ##__VA_ARGS__)
        #endif
    #else
        #define LOG_D(tag, format, ...)  ((void)0)
        #define LOG_V(tag, format, ...)  ((void)0)
    #endif

// ============================================================================
// Generic Implementation (fallback)
// ============================================================================

#else
    #include <stdio.h>
    
    #define LOG_I(tag, format, ...)  printf("I [%s] " format "\n", tag, ##__VA_ARGS__)
    #define LOG_W(tag, format, ...)  printf("W [%s] " format "\n", tag, ##__VA_ARGS__)
    #define LOG_E(tag, format, ...)  printf("E [%s] " format "\n", tag, ##__VA_ARGS__)
    #define LOG_D(tag, format, ...)  printf("D [%s] " format "\n", tag, ##__VA_ARGS__)
    #define LOG_V(tag, format, ...)  printf("V [%s] " format "\n", tag, ##__VA_ARGS__)
#endif

// ============================================================================
// Helper Macros (platform-independent)
// ============================================================================

// Log buffer as hex dump
#define LOG_BUFFER_HEX(tag, buffer, length) \
    do { \
        const uint8_t *_buf = (const uint8_t *)(buffer); \
        LOG_D(tag, "Buffer dump (%u bytes):", (unsigned)(length)); \
        for (size_t _i = 0; _i < (length); _i += 16) { \
            char _line[80]; \
            int _pos = 0; \
            _pos += snprintf(_line + _pos, sizeof(_line) - _pos, "%04x: ", (unsigned)_i); \
            for (size_t _j = 0; _j < 16 && (_i + _j) < (length); _j++) { \
                _pos += snprintf(_line + _pos, sizeof(_line) - _pos, "%02x ", _buf[_i + _j]); \
            } \
            LOG_D(tag, "%s", _line); \
        } \
    } while(0)

// Compile-time log level control
#ifndef LOG_LOCAL_LEVEL
    #define LOG_LOCAL_LEVEL  3  // 0=None, 1=Error, 2=Warn, 3=Info, 4=Debug, 5=Verbose
#endif

#if LOG_LOCAL_LEVEL < 3
    #undef LOG_I
    #define LOG_I(tag, format, ...)  ((void)0)
#endif

#if LOG_LOCAL_LEVEL < 4
    #undef LOG_D
    #define LOG_D(tag, format, ...)  ((void)0)
#endif

#if LOG_LOCAL_LEVEL < 5
    #undef LOG_V
    #define LOG_V(tag, format, ...)  ((void)0)
#endif

#endif // PORTABLE_LOG_H
