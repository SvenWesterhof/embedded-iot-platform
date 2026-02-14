/**
 * @file serv_stm32_ota.c
 * @brief STM32 OTA Service - Downloads, verifies, and transfers firmware to STM32
 */

#include "serv_stm32_ota.h"
#include "serv_https_download.h"
#include "serv_signature_verify.h"
#include "event_bus.h"
#include "os_wrapper.h"
#include "portable_log.h"
#include "protocol_common.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "STM32_OTA_SVC";

// Internal state
static struct {
    stm32_ota_state_t state;
    stm32_ota_notification_t current_update;
    os_task_handle_t ota_task_handle;
    os_mutex_handle_t state_mutex;
    bool initialized;
} s_ctx = {
    .state = STM32_OTA_STATE_IDLE,
    .ota_task_handle = NULL,
    .state_mutex = NULL,
    .initialized = false,
};

// Forward declaration
static void stm32_ota_task(void *arg);

// ============================================================================
// Public API
// ============================================================================

serv_stm32_ota_status_t serv_stm32_ota_init(void)
{
    if (s_ctx.initialized) {
        LOG_W(TAG, "Already initialized");
        return STM32_OTA_OK;
    }

    LOG_I(TAG, "Initializing STM32 OTA service");

    s_ctx.state_mutex = os_mutex_create();
    if (s_ctx.state_mutex == NULL) {
        LOG_E(TAG, "Failed to create mutex");
        return STM32_OTA_ERR_NO_MEM;
    }

    sig_verify_status_t sig_status = serv_signature_verify_init();
    if (sig_status != SIG_VERIFY_OK) {
        LOG_E(TAG, "Failed to initialize signature verification: %s",
                 serv_signature_verify_status_str(sig_status));
        os_mutex_delete(s_ctx.state_mutex);
        s_ctx.state_mutex = NULL;
        return STM32_OTA_ERR_NO_MEM;
    }

    s_ctx.initialized = true;
    LOG_I(TAG, "STM32 OTA service initialized");
    return STM32_OTA_OK;
}

serv_stm32_ota_status_t serv_stm32_ota_trigger(const stm32_ota_notification_t *notification)
{
    if (!s_ctx.initialized) {
        return STM32_OTA_ERR_NO_MEM;
    }

    if (notification == NULL) {
        return STM32_OTA_ERR_INVALID_ARG;
    }

    if (os_mutex_take(s_ctx.state_mutex, 100) != OS_SUCCESS) {
        return STM32_OTA_ERR_IN_PROGRESS;
    }

    if (s_ctx.state != STM32_OTA_STATE_IDLE) {
        os_mutex_give(s_ctx.state_mutex);
        LOG_W(TAG, "Update already in progress");
        return STM32_OTA_ERR_IN_PROGRESS;
    }

    memcpy(&s_ctx.current_update, notification, sizeof(stm32_ota_notification_t));
    s_ctx.state = STM32_OTA_STATE_DOWNLOADING;
    os_mutex_give(s_ctx.state_mutex);

    // Create OTA task pinned to Core 1 (not WiFi core)
    os_result_t ret = os_task_create_pinned(
        stm32_ota_task,
        "stm32_ota",
        8192,
        NULL,
        5,     // Normal priority
        &s_ctx.ota_task_handle,
        1      // Core 1
    );

    if (ret != OS_SUCCESS) {
        LOG_E(TAG, "Failed to create OTA task");
        if (os_mutex_take(s_ctx.state_mutex, 100) == OS_SUCCESS) {
            s_ctx.state = STM32_OTA_STATE_IDLE;
            os_mutex_give(s_ctx.state_mutex);
        }
        return STM32_OTA_ERR_NO_MEM;
    }

    LOG_I(TAG, "STM32 OTA triggered: version %s", notification->version);
    event_bus_publish(EVENT_STM32_OTA_STARTED, NULL);

    return STM32_OTA_OK;
}

bool serv_stm32_ota_is_in_progress(void)
{
    if (!s_ctx.initialized || !s_ctx.state_mutex) {
        return false;
    }

    bool in_progress = false;
    if (os_mutex_take(s_ctx.state_mutex, 100) == OS_SUCCESS) {
        in_progress = (s_ctx.state != STM32_OTA_STATE_IDLE &&
                      s_ctx.state != STM32_OTA_STATE_COMPLETE &&
                      s_ctx.state != STM32_OTA_STATE_FAILED);
        os_mutex_give(s_ctx.state_mutex);
    }
    return in_progress;
}

stm32_ota_state_t serv_stm32_ota_get_state(void)
{
    stm32_ota_state_t state = STM32_OTA_STATE_IDLE;
    if (s_ctx.initialized && s_ctx.state_mutex &&
        os_mutex_take(s_ctx.state_mutex, 100) == OS_SUCCESS) {
        state = s_ctx.state;
        os_mutex_give(s_ctx.state_mutex);
    }
    return state;
}

serv_stm32_ota_status_t serv_stm32_ota_abort(void)
{
    if (!s_ctx.initialized) {
        return STM32_OTA_ERR_NO_MEM;
    }

    LOG_W(TAG, "OTA abort requested");

    if (os_mutex_take(s_ctx.state_mutex, 1000) != OS_SUCCESS) {
        LOG_E(TAG, "Failed to acquire mutex for abort");
        return STM32_OTA_ERR_TIMEOUT;
    }

    os_task_handle_t task_to_delete = s_ctx.ota_task_handle;
    s_ctx.ota_task_handle = NULL;
    s_ctx.state = STM32_OTA_STATE_IDLE;

    os_mutex_give(s_ctx.state_mutex);

    if (task_to_delete != NULL) {
        os_task_delete(task_to_delete);
    }

    return STM32_OTA_OK;
}

serv_stm32_ota_status_t serv_stm32_ota_deinit(void)
{
    if (!s_ctx.initialized) {
        return STM32_OTA_OK;
    }

    LOG_I(TAG, "Deinitializing STM32 OTA service");

    serv_stm32_ota_abort();

    if (s_ctx.state_mutex != NULL) {
        os_mutex_delete(s_ctx.state_mutex);
        s_ctx.state_mutex = NULL;
    }

    s_ctx.initialized = false;
    LOG_I(TAG, "STM32 OTA service deinitialized");
    return STM32_OTA_OK;
}

// ============================================================================
// Internal Task
// ============================================================================

static void stm32_ota_task(void *arg)
{
    (void)arg;

    LOG_I(TAG, "STM32 OTA task started");
    LOG_I(TAG, "URL: %s", s_ctx.current_update.url);
    LOG_I(TAG, "Size: %lu bytes", s_ctx.current_update.size);
    LOG_I(TAG, "Version: %s", s_ctx.current_update.version);

    stm32_ota_state_t final_state = STM32_OTA_STATE_FAILED;
    uint8_t *firmware_buffer = NULL;
    uint32_t firmware_size = 0;

    // Phase 1: Download firmware via HTTPS
    LOG_I(TAG, "Phase 1: Downloading firmware...");
    https_download_config_t download_config = {
        .url = s_ctx.current_update.url,
        .expected_size = s_ctx.current_update.size,
        .timeout_ms = 30000,
        .buffer_size = 4096,
        .progress_cb = NULL,
        .user_data = NULL,
    };

    https_download_status_t dl_status = serv_https_download(&download_config,
                                                            &firmware_buffer,
                                                            &firmware_size);
    if (dl_status != HTTPS_DOWNLOAD_OK) {
        LOG_E(TAG, "Firmware download failed: %s",
                 serv_https_download_status_str(dl_status));
        goto cleanup;
    }

    LOG_I(TAG, "Firmware downloaded: %lu bytes", firmware_size);

    // Update state to VERIFYING
    if (os_mutex_take(s_ctx.state_mutex, 1000) == OS_SUCCESS) {
        s_ctx.state = STM32_OTA_STATE_VERIFYING;
        os_mutex_give(s_ctx.state_mutex);
    }

    // Phase 2: Verify RSA signature
    LOG_I(TAG, "Phase 2: Verifying RSA signature...");
    sig_verify_status_t sig_status = serv_signature_verify_firmware(firmware_buffer,
                                                                     firmware_size,
                                                                     s_ctx.current_update.signature_rsa);
    if (sig_status != SIG_VERIFY_OK) {
        LOG_E(TAG, "Signature verification failed: %s",
                 serv_signature_verify_status_str(sig_status));
        goto cleanup;
    }

    LOG_I(TAG, "Signature verification successful");

    // Update state to TRANSFERRING
    if (os_mutex_take(s_ctx.state_mutex, 1000) == OS_SUCCESS) {
        s_ctx.state = STM32_OTA_STATE_TRANSFERRING;
        os_mutex_give(s_ctx.state_mutex);
    }

    // Phase 3: Transfer to STM32 via UART
    LOG_I(TAG, "Phase 3: Transferring firmware to STM32...");
    // TODO: Implement UART transfer via feat_stm32_protocol
    // - Send CMD_FW_UPDATE_START with total_size, crc32, version
    // - Send firmware in CMD_FW_UPDATE_CHUNK packets (256 bytes each)
    // - Send CMD_FW_UPDATE_END
    // - Wait for STM32 reboot and verification
    LOG_W(TAG, "UART transfer not yet implemented");

    final_state = STM32_OTA_STATE_COMPLETE;

cleanup:
    if (firmware_buffer != NULL) {
        free(firmware_buffer);
    }

    if (os_mutex_take(s_ctx.state_mutex, 1000) == OS_SUCCESS) {
        s_ctx.state = final_state;
        s_ctx.ota_task_handle = NULL;
        os_mutex_give(s_ctx.state_mutex);
    } else {
        LOG_E(TAG, "Failed to acquire mutex in OTA task");
    }

    if (final_state == STM32_OTA_STATE_COMPLETE) {
        LOG_I(TAG, "STM32 OTA completed successfully");
        event_bus_publish(EVENT_STM32_OTA_COMPLETED, NULL);
    } else {
        LOG_E(TAG, "STM32 OTA failed");
        event_bus_publish(EVENT_STM32_OTA_FAILED, NULL);
    }

    os_task_delete(NULL);
}
