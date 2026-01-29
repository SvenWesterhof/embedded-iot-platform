/**
 * @file serv_ntp_sync.h
 * @brief NTP Time Synchronization Service
 * 
 * Background service providing NTP time synchronization:
 * - Initialize SNTP client
 * - Periodic synchronization (configurable interval)
 * - Publish EVENT_NTP_TIME_SYNCED when time is updated
 * - Provide current Unix timestamp
 */

#ifndef SERV_NTP_SYNC_H
#define SERV_NTP_SYNC_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <esp_err.h>

// ============================================================================
// Module-Specific Error Codes
// ============================================================================

/**
 * @brief NTP Sync Service status codes
 * 
 * Module-specific error codes for decoupled error handling.
 * Negative values indicate errors, zero indicates success.
 */
typedef enum {
    NTP_SYNC_OK = 0,                        /**< Success */
    NTP_SYNC_ERR_NOT_INITIALIZED = -1,      /**< Service not initialized */
    NTP_SYNC_ERR_ALREADY_INIT = -2,         /**< Already initialized */
    NTP_SYNC_ERR_INVALID_PARAM = -3,        /**< Invalid parameter */
    NTP_SYNC_ERR_NOT_SYNCED = -4,           /**< Time not yet synchronized */
    NTP_SYNC_ERR_SYNC_FAILED = -5,          /**< Synchronization failed */
    NTP_SYNC_ERR_NO_NETWORK = -6,           /**< Network not available */
    NTP_SYNC_ERR_INVALID_SERVER = -7,       /**< Invalid NTP server */
    NTP_SYNC_ERR_TIMEOUT = -8,              /**< Sync timeout */
} ntp_sync_status_t;

// ============================================================================
// Configuration Constants
// ============================================================================

#define NTP_SERVER_DEFAULT          "pool.ntp.org"
#define NTP_SYNC_INTERVAL_HOURS     24
#define NTP_SYNC_TIMEOUT_MS         15000
#define NTP_RETRY_COUNT             3

// ============================================================================
// Data Types
// ============================================================================

/**
 * @brief NTP sync status
 */
typedef enum {
    NTP_STATUS_NOT_SYNCED = 0,
    NTP_STATUS_SYNCING,
    NTP_STATUS_SYNCED,
    NTP_STATUS_FAILED,
} ntp_status_t;

/**
 * @brief NTP configuration
 */
typedef struct {
    char server[64];                    /**< NTP server address */
    uint8_t sync_interval_hours;        /**< Sync interval in hours */
    int8_t timezone_offset;             /**< Timezone offset from UTC (hours) */
} ntp_config_t;

// ============================================================================
// Public API
// ============================================================================

/**
 * @brief Initialize NTP sync service
 * 
 * Configures SNTP client but does not start synchronization.
 * 
 * @return NTP_SYNC_OK on success, error code otherwise
 */
ntp_sync_status_t serv_ntp_init(void);

/**
 * @brief Start NTP synchronization
 * 
 * Initiates first sync and starts periodic sync task.
 * Requires WiFi to be connected.
 * 
 * @return NTP_SYNC_OK on success, error code otherwise
 */
ntp_sync_status_t serv_ntp_start(void);

/**
 * @brief Stop NTP synchronization
 */
void serv_ntp_stop(void);

/**
 * @brief Trigger immediate NTP sync
 * 
 * @return NTP_SYNC_OK if sync initiated
 */
ntp_sync_status_t serv_ntp_sync_now(void);

/**
 * @brief Get current Unix timestamp
 * 
 * @return Unix timestamp (seconds since epoch), or 0 if not synced
 */
time_t serv_ntp_get_time(void);

/**
 * @brief Get current time as struct tm
 * 
 * @param time_info Pointer to struct tm to fill
 * @return NTP_SYNC_OK on success, NTP_SYNC_ERR_NOT_SYNCED if not synced
 */
ntp_sync_status_t serv_ntp_get_time_info(struct tm *time_info);

/**
 * @brief Check if time is valid (synced)
 * 
 * @return true if time has been synced, false otherwise
 */
bool serv_ntp_is_valid(void);

/**
 * @brief Get NTP sync status
 * 
 * @return Current NTP status
 */
ntp_status_t serv_ntp_get_status(void);

/**
 * @brief Get last sync timestamp
 * 
 * @return Unix timestamp of last successful sync, or 0 if never synced
 */
time_t serv_ntp_get_last_sync(void);

/**
 * @brief Set NTP server
 * 
 * @param server NTP server address
 * @return NTP_SYNC_OK on success
 */
ntp_sync_status_t serv_ntp_set_server(const char *server);

/**
 * @brief Set timezone offset
 * 
 * @param offset_hours Offset from UTC in hours (-12 to +14)
 * @return NTP_SYNC_OK on success
 */
ntp_sync_status_t serv_ntp_set_timezone(int8_t offset_hours);

#endif // SERV_NTP_SYNC_H
