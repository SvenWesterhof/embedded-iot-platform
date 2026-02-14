/**
 * @file serv_esp32_ota.h
 * @brief ESP32 OTA Service - Streaming firmware update via HTTPS
 *
 * Service responsible for executing ESP32 firmware updates:
 * 1. Streams firmware from HTTPS URL directly to flash (esp_https_ota)
 * 2. Validates image via ESP-IDF built-in checks
 * 3. Sets boot partition and optionally reboots
 *
 * Called by cont_ota_manager when target is "esp32".
 * Also provides low-level begin/write/end API for chunk-based updates.
 */

#ifndef SERV_ESP32_OTA_H
#define SERV_ESP32_OTA_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// ESP32 OTA Service Status Codes
typedef enum {
    ESP32_OTA_OK = 0,
    ESP32_OTA_ERR_INVALID_ARG,
    ESP32_OTA_ERR_NOT_INITIALIZED,
    ESP32_OTA_ERR_IN_PROGRESS,
    ESP32_OTA_ERR_NO_PARTITION,
    ESP32_OTA_ERR_WRITE_FAILED,
    ESP32_OTA_ERR_VERIFY_FAILED,
    ESP32_OTA_ERR_SET_BOOT_FAILED,
    ESP32_OTA_ERR_DOWNLOAD_FAILED,
    ESP32_OTA_ERR_INTERNAL
} esp32_ota_status_t;

// ESP32 OTA State
typedef enum {
    ESP32_OTA_STATE_IDLE = 0,
    ESP32_OTA_STATE_DOWNLOADING,
    ESP32_OTA_STATE_VERIFYING,
    ESP32_OTA_STATE_COMPLETE,
    ESP32_OTA_STATE_FAILED,
} esp32_ota_state_t;

// ESP32 OTA Statistics
typedef struct {
    uint32_t total_updates;
    uint32_t successful_updates;
    uint32_t failed_updates;
    uint32_t bytes_written;
    uint32_t last_update_duration_ms;
} esp32_ota_stats_t;

// ESP32 OTA notification (parsed by cont_ota_manager, passed here)
typedef struct {
    char url[256];              // HTTPS firmware URL
    char version[32];           // Firmware version
    size_t expected_size;       // Expected size (0 = auto from Content-Length)
    bool auto_reboot;           // Reboot after successful update
} esp32_ota_notification_t;

/**
 * @brief Initialize ESP32 OTA service
 * @return ESP32_OTA_OK on success
 */
esp32_ota_status_t serv_esp32_ota_init(void);

/**
 * @brief Trigger streaming OTA update from URL
 *
 * Creates a background task that downloads and flashes firmware.
 * Non-blocking — returns immediately after creating the task.
 * Publishes EVENT_OTA_STARTED/PROGRESS/COMPLETED/FAILED on the event bus.
 *
 * @param notification Parsed OTA notification with URL and version
 * @return ESP32_OTA_OK on success
 */
esp32_ota_status_t serv_esp32_ota_trigger(const esp32_ota_notification_t *notification);

/**
 * @brief Check if ESP32 OTA is in progress
 * @return true if update is in progress
 */
bool serv_esp32_ota_is_in_progress(void);

/**
 * @brief Get current ESP32 OTA state
 * @return Current state
 */
esp32_ota_state_t serv_esp32_ota_get_state(void);

/**
 * @brief Get OTA progress (0-100%)
 * @return Progress percentage
 */
uint8_t serv_esp32_ota_get_progress(void);

/**
 * @brief Get OTA statistics
 * @param stats Pointer to stats structure to fill
 */
void serv_esp32_ota_get_stats(esp32_ota_stats_t *stats);

/**
 * @brief Abort ongoing ESP32 OTA update
 * @return ESP32_OTA_OK on success
 */
esp32_ota_status_t serv_esp32_ota_abort(void);

/**
 * @brief Mark current app as valid (prevents rollback)
 * Call after successful boot to cancel rollback protection.
 * @return ESP32_OTA_OK on success
 */
esp32_ota_status_t serv_esp32_ota_mark_app_valid(void);

/**
 * @brief Get running partition label
 * @param label Buffer to store partition label
 * @param label_size Size of label buffer
 * @return ESP32_OTA_OK on success
 */
esp32_ota_status_t serv_esp32_ota_get_running_partition(char *label, size_t label_size);

#endif // SERV_ESP32_OTA_H
