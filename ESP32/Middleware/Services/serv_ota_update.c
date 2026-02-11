/**
 * @file serv_ota_update.c
 * @brief OTA Update Service Implementation
 */

#include "serv_ota_update.h"
#include "portable_log.h"
#include "os_wrapper.h"
#include "../OS/event_bus.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_app_format.h"
#include <string.h>

static const char *TAG = "OTA_SERVICE";

// ============================================================================
// Private State
// ============================================================================

typedef struct {
    bool initialized;
    ota_state_t state;
    esp_ota_handle_t update_handle;
    const esp_partition_t *update_partition;
    const esp_partition_t *running_partition;
    size_t expected_size;
    size_t bytes_written;
    uint32_t start_time_ms;
    char version[32];
    os_mutex_handle_t mutex;
    ota_stats_t stats;
} ota_context_t;

static ota_context_t s_ota = {0};

// ============================================================================
// Private Functions
// ============================================================================

/**
 * @brief Lock OTA context
 */
static inline bool ota_lock(void)
{
    return s_ota.mutex && os_mutex_take(s_ota.mutex, 1000) == OS_SUCCESS;
}

/**
 * @brief Unlock OTA context
 */
static inline void ota_unlock(void)
{
    if (s_ota.mutex) {
        os_mutex_give(s_ota.mutex);
    }
}

/**
 * @brief Publish OTA event
 */
static void ota_publish_event(event_type_t event, void *data)
{
    event_bus_publish(event, data);
}

// ============================================================================
// Public API
// ============================================================================

ota_status_t serv_ota_init(void)
{
    if (s_ota.initialized) {
        LOG_W(TAG, "OTA service already initialized");
        return OTA_OK;
    }

    memset(&s_ota, 0, sizeof(ota_context_t));

    // Create mutex
    s_ota.mutex = os_mutex_create();
    if (!s_ota.mutex) {
        LOG_E(TAG, "Failed to create OTA mutex");
        return OTA_ERR_INTERNAL;
    }

    // Get running partition info
    s_ota.running_partition = esp_ota_get_running_partition();
    if (!s_ota.running_partition) {
        LOG_E(TAG, "Failed to get running partition");
        os_mutex_delete(s_ota.mutex);
        return OTA_ERR_NO_PARTITION;
    }

    LOG_I(TAG, "Running from partition: %s @ 0x%08lx",
          s_ota.running_partition->label,
          s_ota.running_partition->address);

    s_ota.state = OTA_STATE_IDLE;
    s_ota.initialized = true;

    LOG_I(TAG, "OTA service initialized");
    return OTA_OK;
}

ota_status_t serv_ota_begin(size_t expected_size, const char *version)
{
    if (!s_ota.initialized) {
        return OTA_ERR_NOT_INITIALIZED;
    }

    if (!ota_lock()) {
        return OTA_ERR_INTERNAL;
    }

    if (s_ota.state != OTA_STATE_IDLE) {
        LOG_E(TAG, "OTA already in progress");
        ota_unlock();
        return OTA_ERR_IN_PROGRESS;
    }

    if (expected_size == 0) {
        LOG_E(TAG, "Invalid expected size");
        ota_unlock();
        return OTA_ERR_INVALID_ARG;
    }

    // Get next update partition
    s_ota.update_partition = esp_ota_get_next_update_partition(NULL);
    if (!s_ota.update_partition) {
        LOG_E(TAG, "Failed to get update partition");
        ota_unlock();
        return OTA_ERR_NO_PARTITION;
    }

    LOG_I(TAG, "Starting OTA update:");
    LOG_I(TAG, "  Version: %s", version ? version : "unknown");
    LOG_I(TAG, "  Size: %u bytes", expected_size);
    LOG_I(TAG, "  Target partition: %s @ 0x%08lx",
          s_ota.update_partition->label,
          s_ota.update_partition->address);

    // Begin OTA operation
    esp_err_t err = esp_ota_begin(s_ota.update_partition, expected_size, &s_ota.update_handle);
    if (err != ESP_OK) {
        LOG_E(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        ota_unlock();
        return OTA_ERR_INTERNAL;
    }

    // Update state
    s_ota.state = OTA_STATE_IN_PROGRESS;
    s_ota.expected_size = expected_size;
    s_ota.bytes_written = 0;
    s_ota.start_time_ms = os_get_time_ms();

    if (version) {
        strncpy(s_ota.version, version, sizeof(s_ota.version) - 1);
        s_ota.version[sizeof(s_ota.version) - 1] = '\0';
    }

    ota_unlock();

    // Publish event
    ota_publish_event(EVENT_OTA_STARTED, NULL);

    LOG_I(TAG, "OTA update started");
    return OTA_OK;
}

ota_status_t serv_ota_write(const void *data, size_t len)
{
    if (!s_ota.initialized) {
        return OTA_ERR_NOT_INITIALIZED;
    }

    if (!data || len == 0) {
        return OTA_ERR_INVALID_ARG;
    }

    if (!ota_lock()) {
        return OTA_ERR_INTERNAL;
    }

    if (s_ota.state != OTA_STATE_IN_PROGRESS) {
        LOG_E(TAG, "OTA not in progress");
        ota_unlock();
        return OTA_ERR_INTERNAL;
    }

    // Write data
    esp_err_t err = esp_ota_write(s_ota.update_handle, data, len);
    if (err != ESP_OK) {
        LOG_E(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
        s_ota.state = OTA_STATE_ERROR;
        ota_unlock();
        return OTA_ERR_WRITE_FAILED;
    }

    s_ota.bytes_written += len;
    s_ota.stats.bytes_written += len;

    // Log progress every 10%
    uint8_t progress = serv_ota_get_progress();
    static uint8_t last_logged_progress = 0;
    if (progress >= last_logged_progress + 10) {
        LOG_I(TAG, "OTA progress: %u%% (%u / %u bytes)",
              progress, s_ota.bytes_written, s_ota.expected_size);
        last_logged_progress = progress;

        // Publish progress event
        ota_publish_event(EVENT_OTA_PROGRESS, &progress);
    }

    ota_unlock();
    return OTA_OK;
}

ota_status_t serv_ota_end(void)
{
    if (!s_ota.initialized) {
        return OTA_ERR_NOT_INITIALIZED;
    }

    if (!ota_lock()) {
        return OTA_ERR_INTERNAL;
    }

    if (s_ota.state != OTA_STATE_IN_PROGRESS) {
        LOG_E(TAG, "OTA not in progress");
        ota_unlock();
        return OTA_ERR_INTERNAL;
    }

    s_ota.state = OTA_STATE_VERIFYING;
    LOG_I(TAG, "Finalizing OTA update...");

    // End OTA operation (validates image)
    esp_err_t err = esp_ota_end(s_ota.update_handle);
    if (err != ESP_OK) {
        LOG_E(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        s_ota.state = OTA_STATE_ERROR;
        s_ota.stats.failed_updates++;
        ota_unlock();
        ota_publish_event(EVENT_OTA_FAILED, NULL);
        return OTA_ERR_VERIFY_FAILED;
    }

    // Calculate duration
    uint32_t duration_ms = os_get_time_ms() - s_ota.start_time_ms;
    s_ota.stats.last_update_duration_ms = duration_ms;

    LOG_I(TAG, "OTA update verified successfully");
    LOG_I(TAG, "  Bytes written: %u", s_ota.bytes_written);
    LOG_I(TAG, "  Duration: %lu ms", duration_ms);

    s_ota.state = OTA_STATE_COMPLETE;
    s_ota.stats.total_updates++;

    ota_unlock();

    // Publish event
    ota_publish_event(EVENT_OTA_COMPLETED, NULL);

    return OTA_OK;
}

ota_status_t serv_ota_set_boot_partition(void)
{
    if (!s_ota.initialized) {
        return OTA_ERR_NOT_INITIALIZED;
    }

    if (!ota_lock()) {
        return OTA_ERR_INTERNAL;
    }

    if (s_ota.state != OTA_STATE_COMPLETE) {
        LOG_E(TAG, "OTA not complete");
        ota_unlock();
        return OTA_ERR_INTERNAL;
    }

    // Set boot partition
    esp_err_t err = esp_ota_set_boot_partition(s_ota.update_partition);
    if (err != ESP_OK) {
        LOG_E(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        s_ota.stats.failed_updates++;
        ota_unlock();
        return OTA_ERR_SET_BOOT_FAILED;
    }

    LOG_I(TAG, "Boot partition set to: %s", s_ota.update_partition->label);
    LOG_I(TAG, "Reboot to activate new firmware");

    s_ota.stats.successful_updates++;

    // Reset state
    s_ota.state = OTA_STATE_IDLE;
    s_ota.bytes_written = 0;
    s_ota.expected_size = 0;
    memset(s_ota.version, 0, sizeof(s_ota.version));

    ota_unlock();
    return OTA_OK;
}

ota_status_t serv_ota_mark_app_valid(void)
{
    if (!s_ota.initialized) {
        return OTA_ERR_NOT_INITIALIZED;
    }

    // Check if running from OTA partition
    if (s_ota.running_partition->subtype != ESP_PARTITION_SUBTYPE_APP_OTA_0 &&
        s_ota.running_partition->subtype != ESP_PARTITION_SUBTYPE_APP_OTA_1) {
        LOG_I(TAG, "Not running from OTA partition, no validation needed");
        return OTA_OK;
    }

    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err != ESP_OK) {
        LOG_E(TAG, "esp_ota_mark_app_valid_cancel_rollback failed: %s", esp_err_to_name(err));
        return OTA_ERR_INTERNAL;
    }

    LOG_I(TAG, "App marked as valid (rollback cancelled)");
    ota_publish_event(EVENT_OTA_VALIDATED, NULL);

    return OTA_OK;
}

ota_status_t serv_ota_abort(void)
{
    if (!s_ota.initialized) {
        return OTA_ERR_NOT_INITIALIZED;
    }

    if (!ota_lock()) {
        return OTA_ERR_INTERNAL;
    }

    if (s_ota.state == OTA_STATE_IN_PROGRESS || s_ota.state == OTA_STATE_VERIFYING) {
        esp_ota_abort(s_ota.update_handle);
        LOG_W(TAG, "OTA update aborted");
        s_ota.stats.failed_updates++;
    }

    s_ota.state = OTA_STATE_IDLE;
    s_ota.bytes_written = 0;
    s_ota.expected_size = 0;
    memset(s_ota.version, 0, sizeof(s_ota.version));

    ota_unlock();

    ota_publish_event(EVENT_OTA_FAILED, NULL);
    return OTA_OK;
}

void serv_ota_restart(void)
{
    LOG_I(TAG, "Restarting system in 3 seconds...");
    os_delay_ms(3000);
    esp_restart();
}

ota_state_t serv_ota_get_state(void)
{
    return s_ota.state;
}

uint8_t serv_ota_get_progress(void)
{
    if (s_ota.expected_size == 0) {
        return 0;
    }

    uint32_t progress = (s_ota.bytes_written * 100) / s_ota.expected_size;
    return (uint8_t)(progress > 100 ? 100 : progress);
}

void serv_ota_get_stats(ota_stats_t *stats)
{
    if (!stats) {
        return;
    }

    if (ota_lock()) {
        memcpy(stats, &s_ota.stats, sizeof(ota_stats_t));
        ota_unlock();
    }
}

ota_status_t serv_ota_get_running_partition(char *label, size_t label_size)
{
    if (!s_ota.initialized || !label || label_size == 0) {
        return OTA_ERR_INVALID_ARG;
    }

    strncpy(label, s_ota.running_partition->label, label_size - 1);
    label[label_size - 1] = '\0';

    return OTA_OK;
}
