/**
 * @file cont_ota_manager.h
 * @brief OTA Manager Control
 *
 * Industrial OTA workflow:
 * 1. Device connects via MQTT (TLS)
 * 2. Server notifies device of new firmware via MQTT
 * 3. Server sends HTTPS download URL via MQTT
 * 4. Device downloads firmware via HTTPS
 * 5. Device verifies signature
 * 6. Bootloader verifies again (Secure Boot)
 * 7. Device reports success/failure via MQTT
 *
 * Orchestrates OTA process and coordinates with serv_ota_update and serv_mqtt_client.
 */

#ifndef CONT_OTA_MANAGER_H
#define CONT_OTA_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * @brief OTA manager return codes
 */
typedef enum {
    OTA_MGR_OK = 0,
    OTA_MGR_ERR_INVALID_ARG,
    OTA_MGR_ERR_NOT_INITIALIZED,
    OTA_MGR_ERR_IN_PROGRESS,
    OTA_MGR_ERR_DOWNLOAD_FAILED,
    OTA_MGR_ERR_VERIFY_FAILED,
    OTA_MGR_ERR_INTERNAL
} ota_mgr_status_t;

/**
 * @brief OTA notification from server (via MQTT)
 */
typedef struct {
    const char *version;            // New firmware version
    const char *https_url;          // HTTPS URL to firmware binary
    size_t expected_size;           // Expected firmware size
    const char *signature;          // Expected signature (hex string)
    bool auto_reboot;               // Reboot after successful update
} ota_notification_t;

/**
 * @brief Initialize OTA manager
 * Subscribes to MQTT OTA topics
 * @return OTA_MGR_OK on success
 */
ota_mgr_status_t cont_ota_manager_init(void);

/**
 * @brief Start OTA manager
 * Begins listening for OTA notifications
 * @return OTA_MGR_OK on success
 */
ota_mgr_status_t cont_ota_manager_start(void);

/**
 * @brief Trigger HTTPS OTA update (for testing or manual updates)
 * @param notification OTA notification parameters
 * @return OTA_MGR_OK on success
 */
ota_mgr_status_t cont_ota_trigger_update(const ota_notification_t *notification);

/**
 * @brief Cancel ongoing OTA update
 * @return OTA_MGR_OK on success
 */
ota_mgr_status_t cont_ota_cancel_update(void);

/**
 * @brief Validate app after boot (call from app_main after init)
 * Marks app as valid if booted from OTA partition
 * Reports validation success via MQTT
 */
void cont_ota_validate_after_boot(void);

/**
 * @brief Check if OTA update is in progress
 * @return true if update in progress
 */
bool cont_ota_is_update_in_progress(void);

/**
 * @brief Get current OTA progress (0-100%)
 * @return Progress percentage
 */
uint8_t cont_ota_get_progress(void);

/**
 * @brief Get current partition information
 * @param buffer Buffer to write partition info string
 * @param buffer_size Size of buffer
 * @return OTA_MGR_OK on success
 */
ota_mgr_status_t cont_ota_get_partition_info(char *buffer, size_t buffer_size);

#endif // CONT_OTA_MANAGER_H
