/**
 * @file serv_stm32_ota.h
 * @brief STM32 OTA Service - Downloads, verifies, and transfers firmware to STM32
 *
 * Service responsible for executing STM32 firmware updates:
 * 1. Downloads firmware from HTTPS URL to RAM (serv_https_download)
 * 2. Verifies RSA signature (serv_signature_verify)
 * 3. Transfers to STM32 via UART (feat_stm32_protocol)
 *
 * Called by cont_ota_manager when target is "stm32".
 */

#ifndef SERV_STM32_OTA_H
#define SERV_STM32_OTA_H

#include <stdint.h>
#include <stdbool.h>

// STM32 OTA Service Status Codes
typedef enum {
    STM32_OTA_OK = 0,
    STM32_OTA_ERR_INVALID_ARG,
    STM32_OTA_ERR_NO_MEM,
    STM32_OTA_ERR_DOWNLOAD_FAILED,
    STM32_OTA_ERR_SIGNATURE_INVALID,
    STM32_OTA_ERR_STM32_COMM,
    STM32_OTA_ERR_IN_PROGRESS,
    STM32_OTA_ERR_TIMEOUT,
} serv_stm32_ota_status_t;

// STM32 OTA State
typedef enum {
    STM32_OTA_STATE_IDLE = 0,
    STM32_OTA_STATE_DOWNLOADING,
    STM32_OTA_STATE_VERIFYING,
    STM32_OTA_STATE_TRANSFERRING,
    STM32_OTA_STATE_COMPLETE,
    STM32_OTA_STATE_FAILED,
} stm32_ota_state_t;

// STM32 OTA Notification (parsed by cont_ota_manager, passed here)
typedef struct {
    char target[16];            // "stm32"
    char version[32];           // Firmware version
    char url[256];              // HTTPS download URL
    uint32_t size;              // Firmware size in bytes
    char sha256[65];            // SHA256 checksum (hex, optional)
    char signature_rsa[512];    // RSA-2048 signature (base64)
    uint32_t crc32;             // CRC32 checksum (optional)
    bool auto_apply;            // Auto-apply after download
} stm32_ota_notification_t;

/**
 * @brief Initialize STM32 OTA service
 *
 * Sets up signature verification and internal state.
 *
 * @return STM32_OTA_OK on success
 */
serv_stm32_ota_status_t serv_stm32_ota_init(void);

/**
 * @brief Trigger STM32 firmware update
 *
 * Creates a background task that downloads, verifies, and transfers firmware.
 *
 * @param notification Parsed OTA notification with firmware details
 * @return STM32_OTA_OK on success
 */
serv_stm32_ota_status_t serv_stm32_ota_trigger(const stm32_ota_notification_t *notification);

/**
 * @brief Check if STM32 OTA is in progress
 * @return true if update is in progress
 */
bool serv_stm32_ota_is_in_progress(void);

/**
 * @brief Get current STM32 OTA state
 * @return Current state
 */
stm32_ota_state_t serv_stm32_ota_get_state(void);

/**
 * @brief Abort ongoing STM32 OTA update
 * @return STM32_OTA_OK on success
 */
serv_stm32_ota_status_t serv_stm32_ota_abort(void);

/**
 * @brief Deinitialize STM32 OTA service
 * @return STM32_OTA_OK on success
 */
serv_stm32_ota_status_t serv_stm32_ota_deinit(void);

#endif // SERV_STM32_OTA_H
