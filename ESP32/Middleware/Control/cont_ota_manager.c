/**
 * @file cont_ota_manager.c
 * @brief OTA Manager Control Implementation
 */

#include "cont_ota_manager.h"
#include "../Services/serv_ota_update.h"
#include "../Services/serv_mqtt_client.h"
#include "portable_log.h"
#include "os_wrapper.h"
#include "../OS/event_bus.h"
#include "esp_https_ota.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "OTA_MANAGER";

// Note: MQTT topic macros are defined in serv_mqtt_client.h
// - MQTT_TOPIC_GLOBAL_OTA_NOTIFY
// - MQTT_TOPIC_DEVICE_OTA_STATUS_FMT
// - MQTT_TOPIC_DEVICE_OTA_PROGRESS_FMT

// ============================================================================
// Private State
// ============================================================================

typedef struct {
    bool initialized;
    bool update_in_progress;
    os_task_handle_t ota_task_handle;
    char https_url[256];
    char version[32];
    bool auto_reboot;
    os_mutex_handle_t mutex;
} ota_mgr_context_t;

static ota_mgr_context_t s_mgr = {0};

// ============================================================================
// Private Functions
// ============================================================================

/**
 * @brief Lock manager context
 */
static inline bool mgr_lock(void)
{
    return s_mgr.mutex && os_mutex_take(s_mgr.mutex, 1000) == OS_SUCCESS;
}

/**
 * @brief Unlock manager context
 */
static inline void mgr_unlock(void)
{
    if (s_mgr.mutex) {
        os_mutex_give(s_mgr.mutex);
    }
}

/**
 * @brief Report OTA status via MQTT
 */
static void report_ota_status(const char *status, const char *message)
{
    // Get device ID from MQTT client
    const char *device_id = serv_mqtt_get_device_id();
    if (!device_id) {
        LOG_W(TAG, "Cannot report status: device ID not available");
        return;
    }

    // Build device-specific topic using macro
    char topic[128];
    snprintf(topic, sizeof(topic), MQTT_TOPIC_DEVICE_OTA_STATUS_FMT, device_id);

    // Build JSON payload
    char payload[256];
    snprintf(payload, sizeof(payload),
             "{\"status\":\"%s\",\"message\":\"%s\",\"version\":\"%s\"}",
             status, message, s_mgr.version);

    serv_mqtt_publish(topic, payload, strlen(payload), 1, false);
    LOG_I(TAG, "OTA Status: %s - %s", status, message);
}

/**
 * @brief Report OTA progress via MQTT
 */
static void report_ota_progress(uint8_t progress)
{
    // Get device ID from MQTT client
    const char *device_id = serv_mqtt_get_device_id();
    if (!device_id) {
        return;  // Silently fail for progress updates
    }

    // Build device-specific topic using macro
    char topic[128];
    snprintf(topic, sizeof(topic), MQTT_TOPIC_DEVICE_OTA_PROGRESS_FMT, device_id);

    // Build JSON payload
    char payload[64];
    snprintf(payload, sizeof(payload), "{\"progress\":%u}", progress);

    serv_mqtt_publish(topic, payload, strlen(payload), 0, false);
}

/**
 * @brief HTTP event handler for HTTPS OTA
 */
static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
        case HTTP_EVENT_ERROR:
            LOG_E(TAG, "HTTP error");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            LOG_I(TAG, "Connected to server");
            break;
        case HTTP_EVENT_HEADER_SENT:
            break;
        case HTTP_EVENT_ON_HEADER:
            break;
        case HTTP_EVENT_ON_DATA:
            // Data received, progress handled by esp_https_ota
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

/**
 * @brief OTA download and flash task
 */
static void ota_task(void *arg)
{
    (void)arg;

    LOG_I(TAG, "OTA task started");
    report_ota_status("started", "Downloading firmware");

    // Configure HTTPS OTA
    esp_http_client_config_t http_config = {
        .url = s_mgr.https_url,
        .event_handler = http_event_handler,
        .crt_bundle_attach = esp_crt_bundle_attach,  // Use certificate bundle
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
        report_ota_status("failed", "Failed to start download");
        goto ota_task_exit;
    }

    LOG_I(TAG, "Downloading firmware...");

    // Download and flash firmware in chunks
    uint8_t last_progress = 0;
    while (1) {
        err = esp_https_ota_perform(ota_handle);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            break;
        }

        // Report progress
        int image_len_read = esp_https_ota_get_image_len_read(ota_handle);
        int image_size = esp_https_ota_get_image_size(ota_handle);
        uint8_t progress = (image_size > 0) ? (image_len_read * 100 / image_size) : 0;

        if (progress >= last_progress + 10) {
            LOG_I(TAG, "Download progress: %u%% (%d / %d bytes)",
                  progress, image_len_read, image_size);
            report_ota_progress(progress);
            last_progress = progress;
        }

        os_delay_ms(100);  // Yield to other tasks
    }

    // Check result
    if (esp_https_ota_is_complete_data_received(ota_handle) != true) {
        LOG_E(TAG, "Complete data not received");
        report_ota_status("failed", "Incomplete download");
        goto ota_task_cleanup;
    }

    // Finish OTA (verifies image)
    err = esp_https_ota_finish(ota_handle);
    if (err != ESP_OK) {
        LOG_E(TAG, "HTTPS OTA finish failed: %s", esp_err_to_name(err));
        report_ota_status("failed", "Firmware verification failed");
        goto ota_task_exit;
    }

    LOG_I(TAG, "OTA update completed successfully");
    report_ota_status("completed", "Firmware downloaded and verified");
    report_ota_progress(100);

    // Auto reboot if requested
    if (s_mgr.auto_reboot) {
        LOG_I(TAG, "Auto-reboot enabled, restarting in 5 seconds...");
        os_delay_ms(5000);
        esp_restart();
    } else {
        LOG_I(TAG, "Firmware ready, reboot required to activate");
    }

    goto ota_task_exit;

ota_task_cleanup:
    esp_https_ota_abort(ota_handle);

ota_task_exit:
    if (mgr_lock()) {
        s_mgr.update_in_progress = false;
        s_mgr.ota_task_handle = NULL;
        mgr_unlock();
    }
    os_task_delete(NULL);
}

/**
 * @brief Parse MQTT OTA notification JSON payload
 *
 * Expected minimal format:
 * {
 *   "version": "1.1.0",
 *   "url": "https://server.s3.amazonaws.com/firmware/esp32-v1.1.0.bin",
 *   "size": 1998432,
 *   "sha256": "e3b0c44...",
 *   "auto_reboot": false
 * }
 */
static ota_notification_t* parse_ota_notification(const char *json_str)
{
    if (!json_str) {
        return NULL;
    }

    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        LOG_E(TAG, "JSON parse error: %s", cJSON_GetErrorPtr());
        return NULL;
    }

    // Extract required fields
    cJSON *version_obj = cJSON_GetObjectItem(root, "version");
    cJSON *url_obj = cJSON_GetObjectItem(root, "url");

    // Validate required fields
    if (!cJSON_IsString(version_obj) || !cJSON_IsString(url_obj)) {
        LOG_E(TAG, "Missing required fields: version or url");
        cJSON_Delete(root);
        return NULL;
    }

    // Extract optional fields
    cJSON *size_obj = cJSON_GetObjectItem(root, "size");
    cJSON *sha256_obj = cJSON_GetObjectItem(root, "sha256");
    cJSON *auto_reboot_obj = cJSON_GetObjectItem(root, "auto_reboot");

    // Allocate and populate notification structure
    static ota_notification_t notification;  // Static to avoid stack issues
    notification.version = strdup(version_obj->valuestring);
    notification.https_url = strdup(url_obj->valuestring);
    notification.expected_size = cJSON_IsNumber(size_obj) ? size_obj->valueint : 0;
    notification.signature = cJSON_IsString(sha256_obj) ? strdup(sha256_obj->valuestring) : NULL;
    notification.auto_reboot = cJSON_IsBool(auto_reboot_obj) ? cJSON_IsTrue(auto_reboot_obj) : false;

    LOG_I(TAG, "Parsed OTA notification - Version: %s, URL: %s, Size: %u",
          notification.version, notification.https_url, notification.expected_size);

    cJSON_Delete(root);
    return &notification;
}

/**
 * @brief Handle MQTT OTA notification
 */
static void on_mqtt_ota_notify(event_type_t type, void *data)
{
    (void)type;

    if (!data) {
        LOG_W(TAG, "Received null MQTT data for OTA notification");
        return;
    }

    // Expect JSON string payload
    const char *json_payload = (const char *)data;
    LOG_I(TAG, "OTA notification received via MQTT");
    LOG_D(TAG, "Payload: %s", json_payload);

    // Parse JSON notification
    ota_notification_t *notification = parse_ota_notification(json_payload);
    if (!notification) {
        LOG_E(TAG, "Failed to parse OTA notification JSON");
        return;
    }

    // Trigger OTA update
    LOG_I(TAG, "Triggering OTA update to version %s", notification->version);
    ota_mgr_status_t status = cont_ota_trigger_update(notification);

    if (status != OTA_MGR_OK) {
        LOG_E(TAG, "Failed to trigger OTA update: error code %d", status);
        // Optionally send error status via MQTT
    }

    // Free dynamically allocated strings
    free((void*)notification->version);
    free((void*)notification->https_url);
    if (notification->signature) {
        free((void*)notification->signature);
    }
}

// ============================================================================
// Public API
// ============================================================================

ota_mgr_status_t cont_ota_manager_init(void)
{
    if (s_mgr.initialized) {
        LOG_W(TAG, "OTA manager already initialized");
        return OTA_MGR_OK;
    }

    memset(&s_mgr, 0, sizeof(ota_mgr_context_t));

    // Create mutex
    s_mgr.mutex = os_mutex_create();
    if (!s_mgr.mutex) {
        LOG_E(TAG, "Failed to create mutex");
        return OTA_MGR_ERR_INTERNAL;
    }

    // Initialize OTA service
    ota_status_t ota_status = serv_ota_init();
    if (ota_status != OTA_OK) {
        LOG_E(TAG, "Failed to initialize OTA service");
        os_mutex_delete(s_mgr.mutex);
        return OTA_MGR_ERR_INTERNAL;
    }

    s_mgr.initialized = true;
    LOG_I(TAG, "OTA manager initialized");

    return OTA_MGR_OK;
}

ota_mgr_status_t cont_ota_manager_start(void)
{
    if (!s_mgr.initialized) {
        LOG_E(TAG, "Cannot start: OTA manager not initialized");
        return OTA_MGR_ERR_NOT_INITIALIZED;
    }

    LOG_I(TAG, "Starting OTA manager...");

    // Subscribe to global MQTT OTA notification topic
    int msg_id = serv_mqtt_subscribe(MQTT_TOPIC_GLOBAL_OTA_NOTIFY, 1);
    if (msg_id >= 0) {
        LOG_I(TAG, "Subscribed to MQTT topic: %s (msg_id=%d)", MQTT_TOPIC_GLOBAL_OTA_NOTIFY, msg_id);
    } else {
        LOG_W(TAG, "Failed to subscribe to MQTT OTA topic (broker may not be connected yet)");
        // This is non-fatal - subscription will retry on reconnect
    }

    // Subscribe to MQTT data events via event bus
    event_bus_subscribe(EVENT_MQTT_DATA_RECEIVED, on_mqtt_ota_notify);

    LOG_I(TAG, "OTA manager started - listening for firmware update notifications");
    return OTA_MGR_OK;
}

ota_mgr_status_t cont_ota_trigger_update(const ota_notification_t *notification)
{
    if (!s_mgr.initialized) {
        return OTA_MGR_ERR_NOT_INITIALIZED;
    }

    if (!notification || !notification->https_url || !notification->version) {
        return OTA_MGR_ERR_INVALID_ARG;
    }

    if (!mgr_lock()) {
        return OTA_MGR_ERR_INTERNAL;
    }

    if (s_mgr.update_in_progress) {
        LOG_E(TAG, "OTA update already in progress");
        mgr_unlock();
        return OTA_MGR_ERR_IN_PROGRESS;
    }

    // Copy parameters
    strncpy(s_mgr.https_url, notification->https_url, sizeof(s_mgr.https_url) - 1);
    s_mgr.https_url[sizeof(s_mgr.https_url) - 1] = '\0';

    strncpy(s_mgr.version, notification->version, sizeof(s_mgr.version) - 1);
    s_mgr.version[sizeof(s_mgr.version) - 1] = '\0';

    s_mgr.auto_reboot = notification->auto_reboot;
    s_mgr.update_in_progress = true;

    mgr_unlock();

    LOG_I(TAG, "Triggering OTA update:");
    LOG_I(TAG, "  Version: %s", s_mgr.version);
    LOG_I(TAG, "  URL: %s", s_mgr.https_url);
    LOG_I(TAG, "  Auto-reboot: %s", s_mgr.auto_reboot ? "yes" : "no");

    // Create OTA task (large stack for HTTPS + TLS)
    os_result_t result = os_task_create(
        ota_task,
        "ota_task",
        8192,  // 8KB stack for HTTPS + TLS
        NULL,
        OS_PRIORITY_NORMAL,
        &s_mgr.ota_task_handle
    );

    if (result != OS_SUCCESS) {
        LOG_E(TAG, "Failed to create OTA task");
        if (mgr_lock()) {
            s_mgr.update_in_progress = false;
            mgr_unlock();
        }
        return OTA_MGR_ERR_INTERNAL;
    }

    return OTA_MGR_OK;
}

ota_mgr_status_t cont_ota_cancel_update(void)
{
    if (!s_mgr.initialized) {
        return OTA_MGR_ERR_NOT_INITIALIZED;
    }

    if (!mgr_lock()) {
        return OTA_MGR_ERR_INTERNAL;
    }

    if (!s_mgr.update_in_progress) {
        mgr_unlock();
        return OTA_MGR_OK;
    }

    // Delete OTA task if running
    if (s_mgr.ota_task_handle) {
        os_task_delete(s_mgr.ota_task_handle);
        s_mgr.ota_task_handle = NULL;
    }

    s_mgr.update_in_progress = false;
    mgr_unlock();

    LOG_W(TAG, "OTA update cancelled");
    report_ota_status("cancelled", "Update cancelled by user");

    return OTA_MGR_OK;
}

void cont_ota_validate_after_boot(void)
{
    if (!s_mgr.initialized) {
        return;
    }

    // Get running partition to determine if we're on OTA or factory
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (!running) {
        LOG_E(TAG, "Failed to get running partition");
        return;
    }

    // Check if running from OTA partition
    bool is_ota = (running->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0 ||
                   running->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_1);

    // Mark app as valid (prevents rollback)
    ota_status_t status = serv_ota_mark_app_valid();
    if (status == OTA_OK) {
        if (is_ota) {
            LOG_I(TAG, "OTA firmware validated after boot");
            report_ota_status("validated", "New firmware running successfully");
        } else {
            LOG_I(TAG, "Factory firmware validated (no OTA performed)");
            report_ota_status("ready", "Factory firmware active, OTA ready");
        }
    } else {
        LOG_E(TAG, "Failed to mark app as valid (status=%d)", status);
    }
}

bool cont_ota_is_update_in_progress(void)
{
    return s_mgr.update_in_progress;
}

uint8_t cont_ota_get_progress(void)
{
    return serv_ota_get_progress();
}

ota_mgr_status_t cont_ota_get_partition_info(char *buffer, size_t buffer_size)
{
    if (!buffer || buffer_size == 0) {
        return OTA_MGR_ERR_INVALID_ARG;
    }

    const esp_partition_t *running = esp_ota_get_running_partition();
    if (!running) {
        snprintf(buffer, buffer_size, "Unknown partition");
        return OTA_MGR_ERR_INTERNAL;
    }

    // Determine partition type
    const char *type_str = "Unknown";
    if (running->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY) {
        type_str = "factory";
    } else if (running->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0) {
        type_str = "ota_0";
    } else if (running->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_1) {
        type_str = "ota_1";
    }

    snprintf(buffer, buffer_size, "%s @ 0x%08lx", type_str, running->address);
    return OTA_MGR_OK;
}
