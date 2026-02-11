/**
 * @file serv_ota_update.h
 * @brief OTA Update Service
 *
 * Low-level OTA service handling firmware updates with rollback protection.
 * Follows ESP-IDF esp_ota_ops API patterns.
 */

#ifndef SERV_OTA_UPDATE_H
#define SERV_OTA_UPDATE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * @brief OTA service return codes
 */
typedef enum {
    OTA_OK = 0,
    OTA_ERR_INVALID_ARG,
    OTA_ERR_NOT_INITIALIZED,
    OTA_ERR_IN_PROGRESS,
    OTA_ERR_NO_PARTITION,
    OTA_ERR_WRITE_FAILED,
    OTA_ERR_VERIFY_FAILED,
    OTA_ERR_SET_BOOT_FAILED,
    OTA_ERR_INTERNAL
} ota_status_t;

/**
 * @brief OTA update state
 */
typedef enum {
    OTA_STATE_IDLE,
    OTA_STATE_IN_PROGRESS,
    OTA_STATE_VERIFYING,
    OTA_STATE_COMPLETE,
    OTA_STATE_ERROR
} ota_state_t;

/**
 * @brief OTA statistics
 */
typedef struct {
    uint32_t total_updates;
    uint32_t successful_updates;
    uint32_t failed_updates;
    uint32_t bytes_written;
    uint32_t last_update_duration_ms;
} ota_stats_t;

/**
 * @brief Initialize OTA service
 * @return OTA_OK on success
 */
ota_status_t serv_ota_init(void);

/**
 * @brief Begin OTA update process
 * @param expected_size Expected firmware size in bytes
 * @param version Version string (for logging)
 * @return OTA_OK on success
 */
ota_status_t serv_ota_begin(size_t expected_size, const char *version);

/**
 * @brief Write firmware data chunk
 * @param data Firmware data buffer
 * @param len Length of data
 * @return OTA_OK on success
 */
ota_status_t serv_ota_write(const void *data, size_t len);

/**
 * @brief Finalize OTA update and verify
 * @return OTA_OK on success
 */
ota_status_t serv_ota_end(void);

/**
 * @brief Set boot partition to newly written firmware
 * @return OTA_OK on success
 */
ota_status_t serv_ota_set_boot_partition(void);

/**
 * @brief Mark current app as valid (prevents rollback)
 * Call this after successful boot to cancel rollback protection
 * @return OTA_OK on success
 */
ota_status_t serv_ota_mark_app_valid(void);

/**
 * @brief Abort current OTA update
 * @return OTA_OK on success
 */
ota_status_t serv_ota_abort(void);

/**
 * @brief Restart system
 */
void serv_ota_restart(void);

/**
 * @brief Get current OTA state
 * @return Current OTA state
 */
ota_state_t serv_ota_get_state(void);

/**
 * @brief Get OTA progress (0-100%)
 * @return Progress percentage
 */
uint8_t serv_ota_get_progress(void);

/**
 * @brief Get OTA statistics
 * @param stats Pointer to stats structure to fill
 */
void serv_ota_get_stats(ota_stats_t *stats);

/**
 * @brief Get running partition info
 * @param label Buffer to store partition label
 * @param label_size Size of label buffer
 * @return OTA_OK on success
 */
ota_status_t serv_ota_get_running_partition(char *label, size_t label_size);

#endif // SERV_OTA_UPDATE_H
