/**
 * @file serv_ntp_sync.c
 * @brief NTP Time Synchronization Service Implementation
 */

#include "serv_ntp_sync.h"
#include "event_bus.h"
#include "../../OS/os_wrapper.h"
#include "../Drivers_BSP/Custom/portable_log.h"
#include <esp_sntp.h>
#include <string.h>
#include <sys/time.h>

static const char *TAG = "NTP_SVC";

// ============================================================================
// Internal State
// ============================================================================

typedef struct {
    bool initialized;
    bool started;
    ntp_status_t status;
    ntp_config_t config;
    
    time_t last_sync_time;
    os_task_handle_t sync_task;
} ntp_state_t;

static ntp_state_t state = {0};

// ============================================================================
// Internal Functions
// ============================================================================

/**
 * @brief SNTP sync notification callback
 */
static void time_sync_notification_cb(struct timeval *tv)
{
    LOG_I(TAG, "Time synchronized!");
    
    state.status = NTP_STATUS_SYNCED;
    state.last_sync_time = tv->tv_sec;
    
    // Log the current time
    struct tm timeinfo;
    localtime_r(&tv->tv_sec, &timeinfo);
    LOG_I(TAG, "Current time: %04d-%02d-%02d %02d:%02d:%02d",
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    
    // Publish event
    event_bus_publish(EVENT_NTP_TIME_SYNCED, &tv->tv_sec);
}

/**
 * @brief Wait for time sync with timeout
 */
static bool wait_for_sync(uint32_t timeout_ms)
{
    uint32_t start = xTaskGetTickCount() * portTICK_PERIOD_MS;
    
    while ((xTaskGetTickCount() * portTICK_PERIOD_MS - start) < timeout_ms) {
        if (state.status == NTP_STATUS_SYNCED) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    return false;
}

/**
 * @brief Periodic sync task
 */
static void ntp_sync_task(void *arg)
{
    LOG_I(TAG, "NTP sync task started");
    
    // Calculate sync interval in ticks
    uint32_t interval_ms = state.config.sync_interval_hours * 3600 * 1000;
    
    while (state.started) {
        // Attempt sync
        LOG_I(TAG, "Starting NTP synchronization...");
        state.status = NTP_STATUS_SYNCING;
        
        // SNTP will sync in background, wait for callback
        if (!wait_for_sync(NTP_SYNC_TIMEOUT_MS)) {
            LOG_W(TAG, "NTP sync timeout");
            state.status = NTP_STATUS_FAILED;
            event_bus_publish(EVENT_NTP_SYNC_FAILED, NULL);
        }
        
        // Wait for next sync interval
        // Use small delays to allow task cancellation
        uint32_t remaining = interval_ms;
        while (remaining > 0 && state.started) {
            uint32_t delay = (remaining > 10000) ? 10000 : remaining;
            os_delay_ms(delay);
            remaining -= delay;
        }
    }
    
    LOG_I(TAG, "NTP sync task stopped");
    vTaskDelete(NULL);
}

// ============================================================================
// Public API Implementation
// ============================================================================

ntp_sync_status_t serv_ntp_init(void)
{
    if (state.initialized) {
        LOG_W(TAG, "Already initialized");
        return NTP_SYNC_ERR_ALREADY_INIT;
    }
    
    LOG_I(TAG, "Initializing NTP sync feature");
    
    // Set default configuration
    strncpy(state.config.server, NTP_SERVER_DEFAULT, sizeof(state.config.server) - 1);
    state.config.sync_interval_hours = NTP_SYNC_INTERVAL_HOURS;
    state.config.timezone_offset = 0;  // UTC by default
    
    // Set timezone (UTC by default)
    setenv("TZ", "UTC0", 1);
    tzset();
    
    state.status = NTP_STATUS_NOT_SYNCED;
    state.last_sync_time = 0;
    state.initialized = true;
    
    LOG_I(TAG, "NTP sync service initialized (server: %s)", state.config.server);
    return NTP_SYNC_OK;
}

ntp_sync_status_t serv_ntp_start(void)
{
    if (!state.initialized) {
        LOG_E(TAG, "Not initialized");
        return NTP_SYNC_ERR_NOT_INITIALIZED;
    }
    
    if (state.started) {
        LOG_W(TAG, "Already started");
        return NTP_SYNC_OK;
    }
    
    LOG_I(TAG, "Starting NTP sync");
    
    // Configure SNTP
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, state.config.server);
    esp_sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    esp_sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
    
    // Initialize SNTP
    esp_sntp_init();
    
    state.started = true;
    state.status = NTP_STATUS_SYNCING;
    
    // Create periodic sync task
    os_result_t ret = os_task_create_pinned(ntp_sync_task, "ntp_sync", 3072, NULL, 5,
                                  &state.sync_task, 1);
    if (ret != OS_SUCCESS) {
        LOG_W(TAG, "Failed to create sync task");
        // Non-fatal - SNTP will still work via callbacks
    }
    
    LOG_I(TAG, "NTP sync started");
    return NTP_SYNC_OK;
}

void serv_ntp_stop(void)
{
    if (!state.started) {
        return;
    }
    
    LOG_I(TAG, "Stopping NTP sync");
    
    state.started = false;
    
    // Wait for task to exit
    os_delay_ms(200);
    
    // Stop SNTP
    esp_sntp_stop();
    
    LOG_I(TAG, "NTP sync stopped");
}

ntp_sync_status_t serv_ntp_sync_now(void)
{
    if (!state.started) {
        return NTP_SYNC_ERR_NOT_INITIALIZED;
    }
    
    LOG_I(TAG, "Triggering immediate NTP sync");
    
    // Restart SNTP to force immediate sync
    esp_sntp_stop();
    state.status = NTP_STATUS_SYNCING;
    esp_sntp_init();
    
    return NTP_SYNC_OK;
}

time_t serv_ntp_get_time(void)
{
    if (state.status != NTP_STATUS_SYNCED) {
        return 0;
    }
    
    time_t now;
    time(&now);
    return now;
}

ntp_sync_status_t serv_ntp_get_time_info(struct tm *time_info)
{
    if (time_info == NULL) {
        return NTP_SYNC_ERR_INVALID_PARAM;
    }
    
    if (state.status != NTP_STATUS_SYNCED) {
        return NTP_SYNC_ERR_NOT_SYNCED;
    }
    
    time_t now;
    time(&now);
    localtime_r(&now, time_info);
    
    return NTP_SYNC_OK;
}

bool serv_ntp_is_valid(void)
{
    if (state.status != NTP_STATUS_SYNCED) {
        return false;
    }
    
    // Additional check: time should be after year 2024
    time_t now = serv_ntp_get_time();
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    
    return (timeinfo.tm_year >= 124);  // 124 = 2024 - 1900
}

ntp_status_t serv_ntp_get_status(void)
{
    return state.status;
}

time_t serv_ntp_get_last_sync(void)
{
    return state.last_sync_time;
}

ntp_sync_status_t serv_ntp_set_server(const char *server)
{
    if (server == NULL) {
        return NTP_SYNC_ERR_INVALID_PARAM;
    }
    
    strncpy(state.config.server, server, sizeof(state.config.server) - 1);
    state.config.server[sizeof(state.config.server) - 1] = '\0';
    
    // If running, update SNTP server
    if (state.started) {
        esp_sntp_setservername(0, state.config.server);
    }
    
    LOG_I(TAG, "NTP server set to: %s", state.config.server);
    return NTP_SYNC_OK;
}

ntp_sync_status_t serv_ntp_set_timezone(int8_t offset_hours)
{
    if (offset_hours < -12 || offset_hours > 14) {
        return NTP_SYNC_ERR_INVALID_PARAM;
    }
    
    state.config.timezone_offset = offset_hours;
    
    // Update timezone environment variable
    char tz[16];
    if (offset_hours >= 0) {
        snprintf(tz, sizeof(tz), "UTC-%d", offset_hours);
    } else {
        snprintf(tz, sizeof(tz), "UTC+%d", -offset_hours);
    }
    setenv("TZ", tz, 1);
    tzset();
    
    LOG_I(TAG, "Timezone set to UTC%+d", offset_hours);
    return NTP_SYNC_OK;
}
