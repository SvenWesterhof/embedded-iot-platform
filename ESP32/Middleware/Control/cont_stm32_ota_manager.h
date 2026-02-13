/**
 * @file cont_stm32_ota_manager.h
 * @brief STM32 OTA Manager - Controls firmware updates for STM32 via ESP32 gateway
 *
 * This control component:
 * - Subscribes to MQTT topic for STM32 firmware updates
 * - Downloads STM32 firmware from S3 via HTTPS
 * - Verifies ED25519 signature
 * - Forwards firmware to STM32 in chunks via UART
 * - Reports progress and status via MQTT
 */

#ifndef CONT_STM32_OTA_MANAGER_H
#define CONT_STM32_OTA_MANAGER_H

#include <stdint.h>
#include <stdbool.h>

// OTA Manager Status Codes
typedef enum {
    STM32_OTA_MGR_OK = 0,
    STM32_OTA_MGR_ERR_INVALID_ARG,
    STM32_OTA_MGR_ERR_NO_MEM,
    STM32_OTA_MGR_ERR_DOWNLOAD_FAILED,
    STM32_OTA_MGR_ERR_SIGNATURE_INVALID,
    STM32_OTA_MGR_ERR_STM32_COMM,
    STM32_OTA_MGR_ERR_IN_PROGRESS,
    STM32_OTA_MGR_ERR_TIMEOUT,
} stm32_ota_mgr_status_t;

// OTA Update State
typedef enum {
    STM32_OTA_STATE_IDLE = 0,
    STM32_OTA_STATE_DOWNLOADING,
    STM32_OTA_STATE_VERIFYING,
    STM32_OTA_STATE_TRANSFERRING,
    STM32_OTA_STATE_COMPLETE,
    STM32_OTA_STATE_FAILED,
} stm32_ota_state_t;

// OTA Notification Structure (from MQTT)
typedef struct {
    char version[32];           // Firmware version (e.g., "2.1.0")
    char url[256];              // S3 download URL
    uint32_t size;              // Firmware size in bytes
    char sha256[65];            // SHA256 checksum (hex string)
    char signature_ed25519[128]; // ED25519 signature (base64)
    uint32_t crc32;             // CRC32 checksum
    bool auto_apply;            // Auto-apply after download
} stm32_ota_notification_t;

/**
 * @brief Initialize the STM32 OTA Manager
 *
 * Subscribes to MQTT events and sets up task infrastructure
 *
 * @return STM32_OTA_MGR_OK on success
 */
stm32_ota_mgr_status_t cont_stm32_ota_init(void);

/**
 * @brief Trigger STM32 firmware update
 *
 * Downloads firmware, verifies signature, and forwards to STM32
 *
 * @param notification OTA notification with firmware details
 * @return STM32_OTA_MGR_OK on success, error code otherwise
 */
stm32_ota_mgr_status_t cont_stm32_ota_trigger_update(const stm32_ota_notification_t *notification);

/**
 * @brief Check if STM32 OTA update is in progress
 *
 * @return true if update is in progress
 */
bool cont_stm32_ota_is_update_in_progress(void);

/**
 * @brief Get current STM32 OTA state
 *
 * @return Current OTA state
 */
stm32_ota_state_t cont_stm32_ota_get_state(void);

/**
 * @brief Abort ongoing STM32 OTA update
 *
 * @return STM32_OTA_MGR_OK on success
 */
stm32_ota_mgr_status_t cont_stm32_ota_abort(void);

/**
 * @brief Deinitialize the STM32 OTA Manager
 *
 * Cleans up resources (mutex, event subscriptions)
 *
 * @return STM32_OTA_MGR_OK on success
 */
stm32_ota_mgr_status_t cont_stm32_ota_deinit(void);

#endif // CONT_STM32_OTA_MANAGER_H
