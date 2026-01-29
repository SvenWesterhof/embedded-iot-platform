#include "hal_rtc.h"
#include "stm32f7xx_hal.h"
#include <string.h>

// External RTC handle (should be defined in main.c after CubeMX generation)
extern RTC_HandleTypeDef hrtc;

// Flag to track if RTC is initialized and time is valid
static bool rtc_time_valid = false;

hal_rtc_status_t hal_rtc_init(void) {
    // RTC peripheral is initialized by CubeMX generated code
    // This function just checks if it's ready
    
    // Check if RTC is already initialized (after power-on or reset)
    // If backup domain was not reset, RTC keeps running
    if (__HAL_RTC_IS_CALENDAR_INITIALIZED(&hrtc)) {
        rtc_time_valid = true;
    }
    
    return HAL_RTC_OK;
}

uint32_t hal_rtc_get_timestamp(void) {
    RTC_TimeTypeDef sTime;
    RTC_DateTypeDef sDate;
    
    // Get current time and date from RTC
    HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN);
    
    // Convert to datetime structure
    hal_rtc_datetime_t datetime = {
        .year = 2000 + sDate.Year,  // RTC stores year as offset from 2000
        .month = sDate.Month,
        .day = sDate.Date,
        .hour = sTime.Hours,
        .minute = sTime.Minutes,
        .second = sTime.Seconds,
        .millisecond = 0
    };
    
    // Convert to Unix timestamp
    return hal_rtc_datetime_to_timestamp(&datetime);
}

hal_rtc_status_t hal_rtc_get_time(hal_rtc_time_t *time) {
    if (time == NULL) {
        return HAL_RTC_ERROR;
    }
    
    RTC_TimeTypeDef sTime;
    RTC_DateTypeDef sDate;
    
    // Get current time and date
    HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN);
    
    // Convert to datetime
    hal_rtc_datetime_t datetime = {
        .year = 2000 + sDate.Year,
        .month = sDate.Month,
        .day = sDate.Date,
        .hour = sTime.Hours,
        .minute = sTime.Minutes,
        .second = sTime.Seconds,
        .millisecond = 0
    };
    
    // Get Unix timestamp
    time->seconds = hal_rtc_datetime_to_timestamp(&datetime);
    
    // Calculate milliseconds from subseconds
    // STM32 RTC has subsecond counter that counts down from prediv
    uint32_t subseconds = sTime.SubSeconds;
    uint32_t prediv_s = hrtc.Init.SynchPrediv;
    time->milliseconds = (uint16_t)(((prediv_s - subseconds) * 1000) / (prediv_s + 1));
    
    return HAL_RTC_OK;
}

hal_rtc_status_t hal_rtc_set_time(uint32_t seconds, uint16_t milliseconds) {
    // Convert Unix timestamp to datetime
    hal_rtc_datetime_t datetime;
    hal_rtc_timestamp_to_datetime(seconds, &datetime);
    
    // Set RTC time
    RTC_TimeTypeDef sTime = {0};
    RTC_DateTypeDef sDate = {0};
    
    sTime.Hours = datetime.hour;
    sTime.Minutes = datetime.minute;
    sTime.Seconds = datetime.second;
    sTime.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    sTime.StoreOperation = RTC_STOREOPERATION_RESET;
    
    if (HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BIN) != HAL_OK) {
        return HAL_RTC_ERROR;
    }
    
    sDate.Year = datetime.year - 2000;  // RTC stores year as offset from 2000
    sDate.Month = datetime.month;
    sDate.Date = datetime.day;
    
    // Calculate day of week (0 = Monday, 6 = Sunday)
    // Using Zeller's congruence algorithm
    int y = datetime.year;
    int m = datetime.month;
    if (m < 3) {
        m += 12;
        y--;
    }
    int dow = (datetime.day + (13 * (m + 1)) / 5 + y + y / 4 - y / 100 + y / 400) % 7;
    sDate.WeekDay = (dow == 0) ? 7 : dow;  // RTC expects 1=Monday, 7=Sunday
    
    if (HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BIN) != HAL_OK) {
        return HAL_RTC_ERROR;
    }
    
    rtc_time_valid = true;
    return HAL_RTC_OK;
}

hal_rtc_status_t hal_rtc_get_datetime(hal_rtc_datetime_t *datetime) {
    if (datetime == NULL) {
        return HAL_RTC_ERROR;
    }
    
    RTC_TimeTypeDef sTime;
    RTC_DateTypeDef sDate;
    
    HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN);
    
    datetime->year = 2000 + sDate.Year;
    datetime->month = sDate.Month;
    datetime->day = sDate.Date;
    datetime->hour = sTime.Hours;
    datetime->minute = sTime.Minutes;
    datetime->second = sTime.Seconds;
    
    // Calculate milliseconds from subseconds
    uint32_t subseconds = sTime.SubSeconds;
    uint32_t prediv_s = hrtc.Init.SynchPrediv;
    datetime->millisecond = (uint16_t)(((prediv_s - subseconds) * 1000) / (prediv_s + 1));
    
    return HAL_RTC_OK;
}

hal_rtc_status_t hal_rtc_set_datetime(const hal_rtc_datetime_t *datetime) {
    if (datetime == NULL) {
        return HAL_RTC_ERROR;
    }
    
    // Convert to Unix timestamp and use set_time
    uint32_t timestamp = hal_rtc_datetime_to_timestamp(datetime);
    return hal_rtc_set_time(timestamp, datetime->millisecond);
}

bool hal_rtc_is_time_valid(void) {
    return rtc_time_valid;
}

void hal_rtc_timestamp_to_datetime(uint32_t seconds, hal_rtc_datetime_t *datetime) {
    // Use standard library time functions
    time_t ts = (time_t)seconds;
    struct tm *tm_info = gmtime(&ts);
    
    if (tm_info != NULL) {
        datetime->year = tm_info->tm_year + 1900;
        datetime->month = tm_info->tm_mon + 1;
        datetime->day = tm_info->tm_mday;
        datetime->hour = tm_info->tm_hour;
        datetime->minute = tm_info->tm_min;
        datetime->second = tm_info->tm_sec;
        datetime->millisecond = 0;
    }
}

uint32_t hal_rtc_datetime_to_timestamp(const hal_rtc_datetime_t *datetime) {
    struct tm tm_info = {0};
    
    tm_info.tm_year = datetime->year - 1900;
    tm_info.tm_mon = datetime->month - 1;
    tm_info.tm_mday = datetime->day;
    tm_info.tm_hour = datetime->hour;
    tm_info.tm_min = datetime->minute;
    tm_info.tm_sec = datetime->second;
    
    // Convert to Unix timestamp
    time_t timestamp = mktime(&tm_info);
    
    // Adjust for timezone (mktime assumes local time, we want UTC)
    // For embedded systems, typically work in UTC
    return (uint32_t)timestamp;
}

// ========== Low Power Wakeup Functions ==========

hal_rtc_status_t hal_rtc_set_alarm(uint8_t hour, uint8_t minute, uint8_t second) {
    RTC_AlarmTypeDef sAlarm = {0};
    
    sAlarm.AlarmTime.Hours = hour;
    sAlarm.AlarmTime.Minutes = minute;
    sAlarm.AlarmTime.Seconds = second;
    sAlarm.AlarmTime.SubSeconds = 0;
    sAlarm.AlarmTime.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    sAlarm.AlarmTime.StoreOperation = RTC_STOREOPERATION_RESET;
    
    // Set mask based on what should be matched
    sAlarm.AlarmMask = RTC_ALARMMASK_DATEWEEKDAY;  // Ignore date
    
    if (hour == 0xFF) {
        sAlarm.AlarmMask |= RTC_ALARMMASK_HOURS;
    }
    if (minute == 0xFF) {
        sAlarm.AlarmMask |= RTC_ALARMMASK_MINUTES;
    }
    if (second == 0xFF) {
        sAlarm.AlarmMask |= RTC_ALARMMASK_SECONDS;
    }
    
    sAlarm.AlarmSubSecondMask = RTC_ALARMSUBSECONDMASK_ALL;
    sAlarm.AlarmDateWeekDaySel = RTC_ALARMDATEWEEKDAYSEL_DATE;
    sAlarm.AlarmDateWeekDay = 1;
    sAlarm.Alarm = RTC_ALARM_A;
    
    // Set alarm with interrupt enabled
    if (HAL_RTC_SetAlarm_IT(&hrtc, &sAlarm, RTC_FORMAT_BIN) != HAL_OK) {
        return HAL_RTC_ERROR;
    }
    
    return HAL_RTC_OK;
}

hal_rtc_status_t hal_rtc_disable_alarm(void) {
    if (HAL_RTC_DeactivateAlarm(&hrtc, RTC_ALARM_A) != HAL_OK) {
        return HAL_RTC_ERROR;
    }
    return HAL_RTC_OK;
}

hal_rtc_status_t hal_rtc_set_wakeup_timer(uint16_t seconds) {
    if (seconds == 0 || seconds > 65535) {
        return HAL_RTC_ERROR;
    }
    
    // Disable wakeup timer first
    HAL_RTCEx_DeactivateWakeUpTimer(&hrtc);
    
    // Configure wakeup timer
    // Using CK_SPRE (1 Hz clock) for seconds-based wakeup
    if (HAL_RTCEx_SetWakeUpTimer_IT(&hrtc, seconds, RTC_WAKEUPCLOCK_CK_SPRE_16BITS) != HAL_OK) {
        return HAL_RTC_ERROR;
    }
    
    return HAL_RTC_OK;
}

hal_rtc_status_t hal_rtc_disable_wakeup_timer(void) {
    if (HAL_RTCEx_DeactivateWakeUpTimer(&hrtc) != HAL_OK) {
        return HAL_RTC_ERROR;
    }
    return HAL_RTC_OK;
}
