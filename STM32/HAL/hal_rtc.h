#ifndef HAL_RTC_H
#define HAL_RTC_H

/**
 * @file hal_rtc.h
 * @brief Platform-independent RTC (Real-Time Clock) abstraction layer
 * 
 * This HAL provides consistent RTC API for timestamp management.
 * Supports Unix timestamp format and millisecond precision.
 * Can be synchronized with external time sources (NTP, GPS, etc.)
 */

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

// RTC Status
typedef enum {
    HAL_RTC_OK = 0,
    HAL_RTC_ERROR,
    HAL_RTC_NOT_INITIALIZED
} hal_rtc_status_t;

// RTC Time structure (Unix-style)
typedef struct {
    uint32_t seconds;       // Unix timestamp (seconds since 1970-01-01 00:00:00 UTC)
    uint16_t milliseconds;  // Sub-second precision (0-999)
} hal_rtc_time_t;

// RTC DateTime structure (human-readable)
typedef struct {
    uint16_t year;          // Year (e.g., 2026)
    uint8_t month;          // Month (1-12)
    uint8_t day;            // Day (1-31)
    uint8_t hour;           // Hour (0-23)
    uint8_t minute;         // Minute (0-59)
    uint8_t second;         // Second (0-59)
    uint16_t millisecond;   // Millisecond (0-999)
} hal_rtc_datetime_t;

/**
 * @brief Initialize RTC hardware
 * Sets up RTC peripheral with LSE clock source.
 * If RTC already configured (after reset), keeps existing time.
 * 
 * @return hal_rtc_status_t Status of initialization
 */
hal_rtc_status_t hal_rtc_init(void);

/**
 * @brief Get current Unix timestamp
 * Returns seconds since Unix epoch (1970-01-01 00:00:00 UTC)
 * 
 * @return uint32_t Unix timestamp in seconds
 */
uint32_t hal_rtc_get_timestamp(void);

/**
 * @brief Get current timestamp with millisecond precision
 * 
 * @param time Pointer to time structure to fill
 * @return hal_rtc_status_t Status of operation
 */
hal_rtc_status_t hal_rtc_get_time(hal_rtc_time_t *time);

/**
 * @brief Set RTC time from Unix timestamp
 * Used for synchronization with external time sources (NTP, GPS, etc.)
 * 
 * @param seconds Unix timestamp in seconds
 * @param milliseconds Sub-second component (0-999)
 * @return hal_rtc_status_t Status of operation
 */
hal_rtc_status_t hal_rtc_set_time(uint32_t seconds, uint16_t milliseconds);

/**
 * @brief Get current date/time in human-readable format
 * 
 * @param datetime Pointer to datetime structure to fill
 * @return hal_rtc_status_t Status of operation
 */
hal_rtc_status_t hal_rtc_get_datetime(hal_rtc_datetime_t *datetime);

/**
 * @brief Set RTC time from human-readable date/time
 * 
 * @param datetime Pointer to datetime structure with time to set
 * @return hal_rtc_status_t Status of operation
 */
hal_rtc_status_t hal_rtc_set_datetime(const hal_rtc_datetime_t *datetime);

/**
 * @brief Check if RTC is synchronized/valid
 * Returns false if RTC has not been set or lost power
 * 
 * @return bool True if RTC time is valid
 */
bool hal_rtc_is_time_valid(void);

/**
 * @brief Convert Unix timestamp to human-readable datetime
 * 
 * @param seconds Unix timestamp in seconds
 * @param datetime Pointer to datetime structure to fill
 */
void hal_rtc_timestamp_to_datetime(uint32_t seconds, hal_rtc_datetime_t *datetime);

/**
 * @brief Convert human-readable datetime to Unix timestamp
 * 
 * @param datetime Pointer to datetime structure
 * @return uint32_t Unix timestamp in seconds
 */
uint32_t hal_rtc_datetime_to_timestamp(const hal_rtc_datetime_t *datetime);

// ========== Low Power Wakeup Functions (for future use) ==========
// Note: To use interrupts, enable RTC Alarm/Wakeup in CubeMX NVIC settings
// and implement: HAL_RTC_AlarmAEventCallback() / HAL_RTCEx_WakeUpTimerEventCallback()

/**
 * @brief Set RTC Alarm to trigger at specific time
 * Used for low-power periodic wakeup (e.g., wake up every minute/hour)
 * 
 * @param hour Hour to trigger (0-23), or 0xFF to match any hour
 * @param minute Minute to trigger (0-59), or 0xFF to match any minute
 * @param second Second to trigger (0-59), or 0xFF to match any second
 * @return hal_rtc_status_t Status of operation
 */
hal_rtc_status_t hal_rtc_set_alarm(uint8_t hour, uint8_t minute, uint8_t second);

/**
 * @brief Disable RTC Alarm
 * 
 * @return hal_rtc_status_t Status of operation
 */
hal_rtc_status_t hal_rtc_disable_alarm(void);

/**
 * @brief Set RTC Wakeup Timer for periodic wakeup
 * More flexible than alarm for arbitrary intervals
 * 
 * @param seconds Wakeup interval in seconds (1-65535)
 * @return hal_rtc_status_t Status of operation
 */
hal_rtc_status_t hal_rtc_set_wakeup_timer(uint16_t seconds);

/**
 * @brief Disable RTC Wakeup Timer
 * 
 * @return hal_rtc_status_t Status of operation
 */
hal_rtc_status_t hal_rtc_disable_wakeup_timer(void);

#endif // HAL_RTC_H
