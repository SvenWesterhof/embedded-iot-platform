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
#include "mbedtls/md.h"
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
// Streaming UART Transfer (no large firmware buffer required)
// ============================================================================

#define FW_CHUNK_DATA_SIZE   252  // 256 max payload - 4 byte chunk header
#define FW_CHUNK_TIMEOUT_MS  5000
#define FW_CHUNK_MAX_RETRIES 3
#define FW_END_TIMEOUT_MS    10000
#define FW_ERASE_POLL_MS     1000  // Poll interval while waiting for bank erase
#define FW_ERASE_TIMEOUT_MS  15000 // Max time to wait for bank erase

// Context for the streaming download-to-UART callback
typedef struct {
    mbedtls_md_context_t sha256_ctx;           // Incremental SHA256 over received data
    uint16_t             chunk_index;           // Next UART chunk sequence number
    uint32_t             bytes_forwarded;       // Total bytes forwarded to STM32
    uint8_t              partial[FW_CHUNK_DATA_SIZE]; // Partial UART chunk accumulator
    uint16_t             partial_len;           // Bytes currently in partial[]
    uint8_t              last_progress;         // For 10% progress logging
} stream_ctx_t;

// Send one UART chunk (partial or full) with retry
static bool send_uart_chunk(stream_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    cmd_fw_update_chunk_t chunk = {
        .chunk_index  = ctx->chunk_index,
        .chunk_length = len,
    };
    memcpy(chunk.data, data, len);

    for (int retry = 0; retry <= FW_CHUNK_MAX_RETRIES; retry++) {
        int resp = stm32_protocol_send_command(CMD_FW_UPDATE_CHUNK,
                       &chunk, 4 + len, NULL, NULL, FW_CHUNK_TIMEOUT_MS);
        if (resp == RESP_OK) {
            ctx->bytes_forwarded += len;
            ctx->chunk_index++;

            uint32_t total = s_ctx.current_update.size;
            uint8_t  progress = (total > 0)
                ? (uint8_t)((ctx->bytes_forwarded * 100) / total) : 0;
            if (progress >= ctx->last_progress + 10 || ctx->bytes_forwarded == total) {
                event_bus_publish(EVENT_STM32_OTA_PROGRESS, &progress);
                ctx->last_progress = progress;
                LOG_I(TAG, "Transfer progress: %u%%", progress);
            }
            return true;
        }
        if (retry < FW_CHUNK_MAX_RETRIES) {
            LOG_W(TAG, "Chunk %u failed (resp=%d), retry %d/%d",
                  ctx->chunk_index, resp, retry + 1, FW_CHUNK_MAX_RETRIES);
        }
    }
    LOG_E(TAG, "Chunk %u failed after %d retries", ctx->chunk_index, FW_CHUNK_MAX_RETRIES);
    return false;
}

// https_chunk_cb_t: called per HTTP chunk; updates SHA256 and forwards to STM32
static bool stream_to_stm32_cb(const uint8_t *data, uint32_t len, void *user_data)
{
    stream_ctx_t *ctx = (stream_ctx_t*)user_data;

    // Accumulate SHA256 hash incrementally
    mbedtls_md_update(&ctx->sha256_ctx, data, len);

    // Split HTTP chunk (up to 4096 bytes) into FW_CHUNK_DATA_SIZE UART packets
    uint32_t offset = 0;
    while (offset < len) {
        uint16_t space    = FW_CHUNK_DATA_SIZE - ctx->partial_len;
        uint32_t to_copy  = len - offset;
        if (to_copy > space) to_copy = space;

        memcpy(ctx->partial + ctx->partial_len, data + offset, (uint16_t)to_copy);
        ctx->partial_len += (uint16_t)to_copy;
        offset           += to_copy;

        if (ctx->partial_len == FW_CHUNK_DATA_SIZE) {
            if (!send_uart_chunk(ctx, ctx->partial, FW_CHUNK_DATA_SIZE)) {
                return false;
            }
            ctx->partial_len = 0;
        }
    }
    return true;
}

// Initiate STM32 update and wait for Bank 2 flash erase to complete
static bool stm32_start_and_wait_erase(void)
{
    uint8_t ver_major = 0, ver_minor = 0, ver_patch = 0;
    if (sscanf(s_ctx.current_update.version, "%hhu.%hhu.%hhu",
               &ver_major, &ver_minor, &ver_patch) != 3) {
        LOG_W(TAG, "Failed to parse version string: %s, using 0.0.0", s_ctx.current_update.version);
    }

    cmd_fw_update_start_t start_cmd = {
        .total_size    = s_ctx.current_update.size,
        .crc32         = s_ctx.current_update.crc32,
        .chunk_size    = FW_CHUNK_DATA_SIZE,
        .version_major = ver_major,
        .version_minor = ver_minor,
        .version_patch = ver_patch,
    };

    LOG_I(TAG, "Sending FW_UPDATE_START: %lu bytes, v%u.%u.%u",
          s_ctx.current_update.size, ver_major, ver_minor, ver_patch);

    int resp = stm32_protocol_send_command(CMD_FW_UPDATE_START,
                   &start_cmd, sizeof(start_cmd), NULL, NULL, FW_CHUNK_TIMEOUT_MS);
    if (resp != RESP_OK) {
        LOG_E(TAG, "STM32 rejected FW_UPDATE_START: %d", resp);
        return false;
    }

    LOG_I(TAG, "Waiting for STM32 to erase flash bank...");
    uint32_t erase_start = os_get_time_ms();

    while ((os_get_time_ms() - erase_start) < FW_ERASE_TIMEOUT_MS) {
        os_delay_ms(FW_ERASE_POLL_MS);

        resp_fw_update_status_t fw_status = {0};
        size_t status_len = sizeof(fw_status);
        resp = stm32_protocol_send_command(CMD_FW_UPDATE_STATUS,
                   NULL, 0, &fw_status, &status_len, FW_CHUNK_TIMEOUT_MS);
        if (resp != RESP_OK) {
            LOG_W(TAG, "Status poll failed: %d", resp);
            continue;
        }
        if (fw_status.state == FW_UPDATE_RECEIVING) {
            LOG_I(TAG, "STM32 flash erased, ready to receive chunks");
            return true;
        }
        if (fw_status.state == FW_UPDATE_ERROR) {
            LOG_E(TAG, "STM32 erase failed (error_code=%u)", fw_status.error_code);
            return false;
        }
    }

    LOG_E(TAG, "STM32 erase timed out after %u ms", FW_ERASE_TIMEOUT_MS);
    stm32_protocol_send_command(CMD_FW_UPDATE_ABORT, NULL, 0, NULL, NULL, 2000);
    return false;
}

// ============================================================================
// Internal Task
// ============================================================================

static void stm32_ota_task(void *arg)
{
    (void)arg;

    LOG_I(TAG, "STM32 OTA task started");
    LOG_I(TAG, "Version: %s | Size: %lu bytes", s_ctx.current_update.version,
          s_ctx.current_update.size);

    stm32_ota_state_t final_state = STM32_OTA_STATE_FAILED;
    stream_ctx_t stream_ctx;
    bool sha256_initialized = false;
    bool stm32_update_started = false;

    // -----------------------------------------------------------------------
    // Phase 1: Initiate STM32 update — send header and wait for Bank 2 erase.
    // Done BEFORE the HTTP download so the erase (up to 15s) runs first,
    // and the download starts right when STM32 is ready to receive chunks.
    // -----------------------------------------------------------------------
    LOG_I(TAG, "Phase 1: Initiating STM32 firmware update...");
    if (!stm32_start_and_wait_erase()) {
        LOG_E(TAG, "STM32 failed to start firmware update");
        goto cleanup;
    }
    stm32_update_started = true;

    // -----------------------------------------------------------------------
    // Phase 2: Stream firmware from HTTPS directly to STM32 + SHA256 hash.
    // No large firmware buffer — uses only 4KB at a time.
    // -----------------------------------------------------------------------
    LOG_I(TAG, "Phase 2: Streaming firmware to STM32...");

    if (os_mutex_take(s_ctx.state_mutex, 1000) == OS_SUCCESS) {
        s_ctx.state = STM32_OTA_STATE_TRANSFERRING;
        os_mutex_give(s_ctx.state_mutex);
    }

    memset(&stream_ctx, 0, sizeof(stream_ctx));
    mbedtls_md_init(&stream_ctx.sha256_ctx);

    int md_ret = mbedtls_md_setup(&stream_ctx.sha256_ctx,
                                   mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 0);
    if (md_ret != 0) {
        LOG_E(TAG, "Failed to init SHA256 context: -0x%04X", -md_ret);
        goto cleanup;
    }
    sha256_initialized = true;
    mbedtls_md_starts(&stream_ctx.sha256_ctx);

    https_download_config_t dl_config = {
        .url           = s_ctx.current_update.url,
        .expected_size = s_ctx.current_update.size,
        .timeout_ms    = 0,   // use DEFAULT_TIMEOUT_MS (60s)
        .buffer_size   = 0,   // use DEFAULT_BUFFER_SIZE (4096)
        .progress_cb   = NULL,
        .user_data     = NULL,
    };

    https_download_status_t dl_status = serv_https_download_stream(
        &dl_config, stream_to_stm32_cb, &stream_ctx);

    if (dl_status != HTTPS_DOWNLOAD_OK) {
        LOG_E(TAG, "Streaming download failed: %s",
                 serv_https_download_status_str(dl_status));
        goto cleanup;
    }

    // Flush any remaining partial UART chunk (final chunk < FW_CHUNK_DATA_SIZE bytes)
    if (stream_ctx.partial_len > 0) {
        if (!send_uart_chunk(&stream_ctx, stream_ctx.partial, stream_ctx.partial_len)) {
            LOG_E(TAG, "Failed to flush last UART chunk");
            goto cleanup;
        }
    }

    LOG_I(TAG, "Streaming complete: %lu bytes in %u UART chunks",
          stream_ctx.bytes_forwarded, stream_ctx.chunk_index);

    // -----------------------------------------------------------------------
    // Phase 3: Verify RSA signature over the accumulated SHA256 hash.
    // The 32-byte digest is all we need — full binary is not required.
    // -----------------------------------------------------------------------
    LOG_I(TAG, "Phase 3: Verifying RSA signature...");

    if (os_mutex_take(s_ctx.state_mutex, 1000) == OS_SUCCESS) {
        s_ctx.state = STM32_OTA_STATE_VERIFYING;
        os_mutex_give(s_ctx.state_mutex);
    }

    uint8_t sha256_hash[32];
    mbedtls_md_finish(&stream_ctx.sha256_ctx, sha256_hash);
    mbedtls_md_free(&stream_ctx.sha256_ctx);
    sha256_initialized = false;

    sig_verify_status_t sig_status = serv_signature_verify_hash(
        sha256_hash, s_ctx.current_update.signature_rsa);

    if (sig_status != SIG_VERIFY_OK) {
        LOG_E(TAG, "Signature verification failed: %s — sending ABORT",
                 serv_signature_verify_status_str(sig_status));
        stm32_protocol_send_command(CMD_FW_UPDATE_ABORT, NULL, 0, NULL, NULL, 2000);
        goto cleanup;
    }

    LOG_I(TAG, "Signature verified");

    // -----------------------------------------------------------------------
    // Phase 4: Commit — send FW_UPDATE_END to trigger STM32 reset+copy.
    // -----------------------------------------------------------------------
    LOG_I(TAG, "Phase 4: Committing update (auto_apply=%s)...",
          s_ctx.current_update.auto_apply ? "yes" : "no");

    cmd_fw_update_end_t end_cmd = {
        .validate_only = s_ctx.current_update.auto_apply ? 0 : 1,
    };

    int resp = stm32_protocol_send_command(CMD_FW_UPDATE_END,
                   &end_cmd, sizeof(end_cmd), NULL, NULL, FW_END_TIMEOUT_MS);
    if (resp != RESP_OK) {
        LOG_E(TAG, "FW_UPDATE_END failed: %d", resp);
        goto cleanup;
    }

    LOG_I(TAG, "STM32 firmware update committed successfully");
    final_state = STM32_OTA_STATE_COMPLETE;
    stm32_update_started = false;  // ownership transferred to STM32

cleanup:
    if (sha256_initialized) {
        mbedtls_md_free(&stream_ctx.sha256_ctx);
    }

    // If the update was started but not committed, send ABORT so STM32 clears Bank 2
    if (stm32_update_started && final_state != STM32_OTA_STATE_COMPLETE) {
        stm32_protocol_send_command(CMD_FW_UPDATE_ABORT, NULL, 0, NULL, NULL, 2000);
    }

    if (os_mutex_take(s_ctx.state_mutex, 1000) == OS_SUCCESS) {
        s_ctx.state = final_state;
        s_ctx.ota_task_handle = NULL;
        os_mutex_give(s_ctx.state_mutex);
    } else {
        LOG_E(TAG, "Failed to acquire mutex in OTA task cleanup");
    }

    if (final_state == STM32_OTA_STATE_COMPLETE) {
        LOG_I(TAG, "STM32 OTA completed successfully");
        event_bus_publish(EVENT_STM32_OTA_COMPLETED, NULL);
    } else {
        LOG_E(TAG, "STM32 OTA failed");
        event_bus_publish(EVENT_STM32_OTA_FAILED, NULL);
    }

    // Reset to IDLE so the next trigger can proceed
    if (os_mutex_take(s_ctx.state_mutex, 1000) == OS_SUCCESS) {
        s_ctx.state = STM32_OTA_STATE_IDLE;
        os_mutex_give(s_ctx.state_mutex);
    }

    os_task_delete(NULL);
}
