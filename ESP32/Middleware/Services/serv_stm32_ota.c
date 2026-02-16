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
#include "feat_stm32_protocol.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

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
// UART Firmware Transfer
// ============================================================================

#define FW_CHUNK_DATA_SIZE   252  // 256 max payload - 4 byte chunk header
#define FW_CHUNK_TIMEOUT_MS  5000
#define FW_CHUNK_MAX_RETRIES 3
#define FW_END_TIMEOUT_MS    10000
#define FW_ERASE_POLL_MS     1000  // Poll interval while waiting for bank erase
#define FW_ERASE_TIMEOUT_MS  15000 // Max time to wait for bank erase

static bool transfer_firmware_to_stm32(const uint8_t *fw_data, uint32_t fw_size)
{
    // Parse version string (e.g. "1.2.3")
    uint8_t ver_major = 0, ver_minor = 0, ver_patch = 0;
    sscanf(s_ctx.current_update.version, "%hhu.%hhu.%hhu",
           &ver_major, &ver_minor, &ver_patch);

    // Step 1: Send CMD_FW_UPDATE_START
    cmd_fw_update_start_t start_cmd = {
        .total_size = fw_size,
        .crc32 = s_ctx.current_update.crc32,
        .chunk_size = FW_CHUNK_DATA_SIZE,
        .version_major = ver_major,
        .version_minor = ver_minor,
        .version_patch = ver_patch,
    };

    LOG_I(TAG, "Sending FW_UPDATE_START: %lu bytes, v%u.%u.%u",
          fw_size, ver_major, ver_minor, ver_patch);

    int resp = stm32_protocol_send_command(CMD_FW_UPDATE_START,
                &start_cmd, sizeof(start_cmd), NULL, NULL, FW_CHUNK_TIMEOUT_MS);
    if (resp != RESP_OK) {
        LOG_E(TAG, "STM32 rejected FW_UPDATE_START: %d", resp);
        return false;
    }

    // Step 1b: Poll until STM32 finishes erasing flash bank
    LOG_I(TAG, "Waiting for STM32 to erase flash bank...");
    uint32_t erase_start = os_get_time_ms();
    bool erase_done = false;

    while ((os_get_time_ms() - erase_start) < FW_ERASE_TIMEOUT_MS) {
        os_delay_ms(FW_ERASE_POLL_MS);

        resp_fw_update_status_t fw_status = {0};
        size_t status_len = sizeof(fw_status);
        resp = stm32_protocol_send_command(CMD_FW_UPDATE_STATUS,
                    NULL, 0, &fw_status, &status_len,
                    FW_CHUNK_TIMEOUT_MS);
        if (resp != RESP_OK) {
            LOG_W(TAG, "Status poll failed: %d", resp);
            continue;
        }

        if (fw_status.state == FW_UPDATE_RECEIVING) {
            LOG_I(TAG, "STM32 flash erased, ready to receive chunks");
            erase_done = true;
            break;
        } else if (fw_status.state == FW_UPDATE_ERROR) {
            LOG_E(TAG, "STM32 erase failed (error_code=%u)", fw_status.error_code);
            return false;
        }
        // Still ERASING, keep polling
    }

    if (!erase_done) {
        LOG_E(TAG, "STM32 erase timed out after %u ms", FW_ERASE_TIMEOUT_MS);
        stm32_protocol_send_command(CMD_FW_UPDATE_ABORT, NULL, 0, NULL, NULL, 2000);
        return false;
    }

    // Step 2: Send firmware chunks
    uint16_t total_chunks = (uint16_t)((fw_size + FW_CHUNK_DATA_SIZE - 1) / FW_CHUNK_DATA_SIZE);
    uint8_t last_progress = 0;

    LOG_I(TAG, "Sending %u chunks (%u bytes each)", total_chunks, FW_CHUNK_DATA_SIZE);

    for (uint16_t i = 0; i < total_chunks; i++) {
        uint32_t offset = (uint32_t)i * FW_CHUNK_DATA_SIZE;
        uint32_t remaining = fw_size - offset;
        uint16_t this_len = (remaining > FW_CHUNK_DATA_SIZE)
                            ? FW_CHUNK_DATA_SIZE : (uint16_t)remaining;

        cmd_fw_update_chunk_t chunk = {
            .chunk_index = i,
            .chunk_length = this_len,
        };
        memcpy(chunk.data, fw_data + offset, this_len);

        // Retry logic for individual chunks
        bool chunk_sent = false;
        for (int retry = 0; retry <= FW_CHUNK_MAX_RETRIES; retry++) {
            resp = stm32_protocol_send_command(CMD_FW_UPDATE_CHUNK,
                        &chunk, 4 + this_len, NULL, NULL, FW_CHUNK_TIMEOUT_MS);
            if (resp == RESP_OK) {
                chunk_sent = true;
                break;
            }
            if (retry < FW_CHUNK_MAX_RETRIES) {
                LOG_W(TAG, "Chunk %u/%u failed (resp=%d), retry %d/%d",
                      i, total_chunks, resp, retry + 1, FW_CHUNK_MAX_RETRIES);
            }
        }

        if (!chunk_sent) {
            LOG_E(TAG, "Chunk %u/%u failed after %d retries",
                  i, total_chunks, FW_CHUNK_MAX_RETRIES);
            stm32_protocol_send_command(CMD_FW_UPDATE_ABORT,
                        NULL, 0, NULL, NULL, 2000);
            return false;
        }

        // Progress reporting at ~10% intervals
        uint8_t progress = (uint8_t)((uint32_t)(i + 1) * 100 / total_chunks);
        if (progress >= last_progress + 10 || i == total_chunks - 1) {
            event_bus_publish(EVENT_STM32_OTA_PROGRESS, &progress);
            last_progress = progress;
            LOG_I(TAG, "Transfer progress: %u%%", progress);
        }
    }

    // Step 3: Send CMD_FW_UPDATE_END
    cmd_fw_update_end_t end_cmd = {
        .validate_only = s_ctx.current_update.auto_apply ? 0 : 1,
    };

    LOG_I(TAG, "Sending FW_UPDATE_END (auto_apply=%s)",
          s_ctx.current_update.auto_apply ? "yes" : "no");

    resp = stm32_protocol_send_command(CMD_FW_UPDATE_END,
                &end_cmd, sizeof(end_cmd), NULL, NULL, FW_END_TIMEOUT_MS);
    if (resp != RESP_OK) {
        LOG_E(TAG, "FW_UPDATE_END failed: %d", resp);
        return false;
    }

    LOG_I(TAG, "Firmware transfer completed: %lu bytes in %u chunks",
          fw_size, total_chunks);
    return true;
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
    if (!transfer_firmware_to_stm32(firmware_buffer, firmware_size)) {
        LOG_E(TAG, "UART transfer to STM32 failed");
        goto cleanup;
    }

    LOG_I(TAG, "Firmware transfer to STM32 complete");
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

    // Reset state to IDLE so next OTA trigger can proceed
    if (os_mutex_take(s_ctx.state_mutex, 1000) == OS_SUCCESS) {
        s_ctx.state = STM32_OTA_STATE_IDLE;
        os_mutex_give(s_ctx.state_mutex);
    }

    os_task_delete(NULL);
}
