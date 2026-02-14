/**
 * @file cont_ota_manager.h
 * @brief Unified OTA Manager Controller
 *
 * Single controller for all OTA operations:
 * 1. Subscribes to MQTT OTA notification topic
 * 2. Parses JSON and routes by "target" field
 * 3. ESP32 target → serv_esp32_ota (streaming HTTPS OTA)
 * 4. STM32 target → serv_stm32_ota (download → verify → UART transfer)
 * 5. Reports status/progress via MQTT (subscribes to OTA events)
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
    OTA_MGR_ERR_INTERNAL
} ota_mgr_status_t;

/**
 * @brief Initialize unified OTA manager
 *
 * Initializes both ESP32 OTA service and STM32 OTA service.
 *
 * @return OTA_MGR_OK on success
 */
ota_mgr_status_t cont_ota_manager_init(void);

/**
 * @brief Start OTA manager
 *
 * Subscribes to MQTT OTA topic, event bus OTA events, and begins
 * listening for notifications for both ESP32 and STM32 targets.
 *
 * @return OTA_MGR_OK on success
 */
ota_mgr_status_t cont_ota_manager_start(void);

/**
 * @brief Cancel ongoing OTA update (ESP32 or STM32)
 * @return OTA_MGR_OK on success
 */
ota_mgr_status_t cont_ota_cancel_update(void);

/**
 * @brief Validate app after boot (ESP32 only)
 *
 * Marks app as valid if booted from OTA partition (prevents rollback).
 * Reports validation success via MQTT.
 */
void cont_ota_validate_after_boot(void);

/**
 * @brief Check if any OTA update is in progress (ESP32 or STM32)
 * @return true if update in progress
 */
bool cont_ota_is_update_in_progress(void);

/**
 * @brief Get current ESP32 OTA progress (0-100%)
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
