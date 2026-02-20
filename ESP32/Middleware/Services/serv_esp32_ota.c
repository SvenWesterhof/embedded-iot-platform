/**
 * @file serv_esp32_ota.c
 * @brief ESP32 OTA Service - Streaming firmware update via HTTPS
 *
 * Owns the full OTA lifecycle: task creation, streaming download,
 * flash write, boot partition switch, and optional reboot.
 */

#include "serv_esp32_ota.h"
#include "portable_log.h"
#include "os_wrapper.h"
#include "../OS/event_bus.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_app_format.h"
#include "esp_https_ota.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include <string.h>

static const char *TAG = "ESP32_OTA_SVC";

// ============================================================================
// Private State
// ============================================================================

typedef struct {
    bool initialized;
    esp32_ota_state_t state;
    const esp_partition_t *running_partition;
    size_t expected_size;
    size_t bytes_written;
    uint32_t start_time_ms;
    os_mutex_handle_t mutex;
    os_task_handle_t ota_task_handle;
    esp32_ota_stats_t stats;
    esp32_ota_notification_t current_update;
} esp32_ota_context_t;

static esp32_ota_context_t s_ctx = {0};

// ============================================================================
// Private Helpers
// ============================================================================

static inline bool ctx_lock(void)
{
    return s_ctx.mutex && os_mutex_take(s_ctx.mutex, 1000) == OS_SUCCESS;
}

static inline void ctx_unlock(void)
{
    if (s_ctx.mutex) {
        os_mutex_give(s_ctx.mutex);
    }
}

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
        case HTTP_EVENT_ERROR:
            LOG_E(TAG, "HTTP error");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            LOG_I(TAG, "Connected to server");
            break;
        case HTTP_EVENT_ON_FINISH:
            LOG_I(TAG, "HTTP session finished");
            break;
        case HTTP_EVENT_DISCONNECTED:
            LOG_I(TAG, "Disconnected from server");
            break;
        default:
            break;
    }
    return ESP_OK;
}

// ============================================================================
// OTA Task — streaming download + flash
// ============================================================================

static void esp32_ota_task(void *arg)
{
    (void)arg;

    LOG_I(TAG, "ESP32 OTA task started");
    LOG_I(TAG, "  URL: %s", s_ctx.current_update.url);
    LOG_I(TAG, "  Version: %s", s_ctx.current_update.version);
    LOG_I(TAG, "  Auto-reboot: %s", s_ctx.current_update.auto_reboot ? "yes" : "no");

    event_bus_publish(EVENT_OTA_STARTED, NULL);

    // Configure HTTPS client
    esp_http_client_config_t http_config = {
        .url = s_ctx.current_update.url,
        .event_handler = http_event_handler,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 30000,
        .buffer_size = 4096,
        .buffer_size_tx = 1024,
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
        .http_client_init_cb = NULL,
    };

    esp_https_ota_handle_t ota_handle = NULL;
    esp_err_t err = esp_https_ota_begin(&ota_config, &ota_handle);
    if (err != ESP_OK) {
        LOG_E(TAG, "HTTPS OTA begin failed: %s", esp_err_to_name(err));
        goto task_fail;
    }

    LOG_I(TAG, "Downloading firmware...");

    // Download and flash in chunks
    uint8_t last_progress = 0;

    while (1) {
        err = esp_https_ota_perform(ota_handle);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            break;
        }

        // Track and report progress
        int image_len_read = esp_https_ota_get_image_len_read(ota_handle);
        int image_size = esp_https_ota_get_image_size(ota_handle);
        uint8_t progress = (image_size > 0) ? (image_len_read * 100 / image_size) : 0;

        if (ctx_lock()) {
            s_ctx.bytes_written = image_len_read;
            s_ctx.expected_size = image_size;
            ctx_unlock();
        }

        if (progress >= last_progress + 10) {
            LOG_I(TAG, "Download progress: %u%% (%d / %d bytes)",
                  progress, image_len_read, image_size);
            last_progress = progress;
            event_bus_publish(EVENT_OTA_PROGRESS, &progress);
        }

        os_delay_ms(100);
    }

    // Verify complete data received
    if (!esp_https_ota_is_complete_data_received(ota_handle)) {
        LOG_E(TAG, "Complete data not received");
        esp_https_ota_abort(ota_handle);
        goto task_fail;
    }

    // Finish OTA (validates image and sets boot partition)
    err = esp_https_ota_finish(ota_handle);
    if (err != ESP_OK) {
        LOG_E(TAG, "HTTPS OTA finish failed: %s", esp_err_to_name(err));
        goto task_fail;
    }

    // ── Success ──
    else {
        uint32_t duration_ms = os_get_time_ms() - s_ctx.start_time_ms;

        if (ctx_lock()) {
            s_ctx.state = ESP32_OTA_STATE_COMPLETE;
            s_ctx.stats.total_updates++;
            s_ctx.stats.successful_updates++;
            s_ctx.stats.last_update_duration_ms = duration_ms;
            s_ctx.ota_task_handle = NULL;
            ctx_unlock();
        }

        LOG_I(TAG, "ESP32 OTA completed successfully (duration: %u ms)", duration_ms);

        uint8_t done = 100;
        event_bus_publish(EVENT_OTA_PROGRESS, &done);
        event_bus_publish(EVENT_OTA_COMPLETED, NULL);

        // Auto-reboot if requested
        if (s_ctx.current_update.auto_reboot) {
            LOG_I(TAG, "Auto-reboot enabled, restarting in 5 seconds...");
            os_delay_ms(5000);
            esp_restart();
        } else {
            LOG_I(TAG, "Firmware ready, reboot required to activate");
        }
    }

    os_task_delete(NULL);
    return;

task_fail:
    if (ctx_lock()) {
        s_ctx.state = ESP32_OTA_STATE_FAILED;
        s_ctx.stats.failed_updates++;
        s_ctx.ota_task_handle = NULL;
        ctx_unlock();
    }
    event_bus_publish(EVENT_OTA_FAILED, NULL);
    os_task_delete(NULL);
}

// ============================================================================
// Public API
// ============================================================================

esp32_ota_status_t serv_esp32_ota_init(void)
{
    if (s_ctx.initialized) {
        LOG_W(TAG, "Already initialized");
        return ESP32_OTA_OK;
    }

    memset(&s_ctx, 0, sizeof(esp32_ota_context_t));

    s_ctx.mutex = os_mutex_create();
    if (!s_ctx.mutex) {
        LOG_E(TAG, "Failed to create mutex");
        return ESP32_OTA_ERR_INTERNAL;
    }

    s_ctx.running_partition = esp_ota_get_running_partition();
    if (!s_ctx.running_partition) {
        LOG_E(TAG, "Failed to get running partition");
        os_mutex_delete(s_ctx.mutex);
        return ESP32_OTA_ERR_NO_PARTITION;
    }

    LOG_I(TAG, "Running from partition: %s @ 0x%08x",
          s_ctx.running_partition->label,
          s_ctx.running_partition->address);

    s_ctx.state = ESP32_OTA_STATE_IDLE;
    s_ctx.initialized = true;

    LOG_I(TAG, "ESP32 OTA service initialized");
    return ESP32_OTA_OK;
}

esp32_ota_status_t serv_esp32_ota_trigger(const esp32_ota_notification_t *notification)
{
    if (!s_ctx.initialized) {
        return ESP32_OTA_ERR_NOT_INITIALIZED;
    }

    if (!notification || notification->url[0] == '\0' || notification->version[0] == '\0') {
        return ESP32_OTA_ERR_INVALID_ARG;
    }

    if (!ctx_lock()) {
        return ESP32_OTA_ERR_INTERNAL;
    }

    if (s_ctx.state != ESP32_OTA_STATE_IDLE) {
        LOG_W(TAG, "Update already in progress");
        ctx_unlock();
        return ESP32_OTA_ERR_IN_PROGRESS;
    }

    // Copy notification data
    memcpy(&s_ctx.current_update, notification, sizeof(esp32_ota_notification_t));
    s_ctx.state = ESP32_OTA_STATE_DOWNLOADING;
    s_ctx.bytes_written = 0;
    s_ctx.expected_size = notification->expected_size;
    s_ctx.start_time_ms = os_get_time_ms();

    ctx_unlock();

    LOG_I(TAG, "ESP32 OTA triggered: version %s", notification->version);

    // Create OTA task (8KB stack for HTTPS + TLS)
    os_result_t ret = os_task_create(
        esp32_ota_task,
        "esp32_ota",
        8192,
        NULL,
        OS_PRIORITY_NORMAL,
        &s_ctx.ota_task_handle
    );

    if (ret != OS_SUCCESS) {
        LOG_E(TAG, "Failed to create OTA task");
        if (ctx_lock()) {
            s_ctx.state = ESP32_OTA_STATE_IDLE;
            ctx_unlock();
        }
        return ESP32_OTA_ERR_INTERNAL;
    }

    return ESP32_OTA_OK;
}

bool serv_esp32_ota_is_in_progress(void)
{
    if (!s_ctx.initialized) {
        return false;
    }

    bool in_progress = false;
    if (ctx_lock()) {
        in_progress = (s_ctx.state == ESP32_OTA_STATE_DOWNLOADING ||
                      s_ctx.state == ESP32_OTA_STATE_VERIFYING);
        ctx_unlock();
    }
    return in_progress;
}

esp32_ota_state_t serv_esp32_ota_get_state(void)
{
    esp32_ota_state_t state = ESP32_OTA_STATE_IDLE;
    if (s_ctx.initialized && ctx_lock()) {
        state = s_ctx.state;
        ctx_unlock();
    }
    return state;
}

uint8_t serv_esp32_ota_get_progress(void)
{
    if (s_ctx.expected_size == 0) {
        return 0;
    }
    uint32_t progress = (s_ctx.bytes_written * 100) / s_ctx.expected_size;
    return (uint8_t)(progress > 100 ? 100 : progress);
}

void serv_esp32_ota_get_stats(esp32_ota_stats_t *stats)
{
    if (!stats) {
        return;
    }
    if (ctx_lock()) {
        memcpy(stats, &s_ctx.stats, sizeof(esp32_ota_stats_t));
        ctx_unlock();
    }
}

esp32_ota_status_t serv_esp32_ota_abort(void)
{
    if (!s_ctx.initialized) {
        return ESP32_OTA_ERR_NOT_INITIALIZED;
    }

    LOG_W(TAG, "OTA abort requested");

    if (!ctx_lock()) {
        return ESP32_OTA_ERR_INTERNAL;
    }

    os_task_handle_t task_to_delete = s_ctx.ota_task_handle;
    s_ctx.ota_task_handle = NULL;
    s_ctx.state = ESP32_OTA_STATE_IDLE;

    ctx_unlock();

    if (task_to_delete != NULL) {
        os_task_delete(task_to_delete);
    }

    event_bus_publish(EVENT_OTA_FAILED, NULL);
    return ESP32_OTA_OK;
}

esp32_ota_status_t serv_esp32_ota_mark_app_valid(void)
{
    if (!s_ctx.initialized) {
        return ESP32_OTA_ERR_NOT_INITIALIZED;
    }

    if (s_ctx.running_partition->subtype != ESP_PARTITION_SUBTYPE_APP_OTA_0 &&
        s_ctx.running_partition->subtype != ESP_PARTITION_SUBTYPE_APP_OTA_1) {
        LOG_I(TAG, "Not running from OTA partition, no validation needed");
        return ESP32_OTA_OK;
    }

    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err != ESP_OK) {
        LOG_E(TAG, "esp_ota_mark_app_valid_cancel_rollback failed: %s", esp_err_to_name(err));
        return ESP32_OTA_ERR_INTERNAL;
    }

    LOG_I(TAG, "App marked as valid (rollback cancelled)");
    event_bus_publish(EVENT_OTA_VALIDATED, NULL);
    return ESP32_OTA_OK;
}

esp32_ota_status_t serv_esp32_ota_get_running_partition(char *label, size_t label_size)
{
    if (!s_ctx.initialized || !label || label_size == 0) {
        return ESP32_OTA_ERR_INVALID_ARG;
    }

    strncpy(label, s_ctx.running_partition->label, label_size - 1);
    label[label_size - 1] = '\0';

    return ESP32_OTA_OK;
}
