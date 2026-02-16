/**
 * @file cont_ota_manager.c
 * @brief Unified OTA Manager Controller
 *
 * Pure router + MQTT reporter. No task management or ESP-IDF OTA calls.
 * Routes OTA notifications to the correct service based on "target" field:
 * - "esp32" → serv_esp32_ota
 * - "stm32" → serv_stm32_ota
 * Subscribes to OTA events from both services for MQTT status reporting.
 */

#include "cont_ota_manager.h"
#include "../Services/serv_esp32_ota.h"
#include "../Services/serv_stm32_ota.h"
#include "../Services/serv_mqtt_client.h"
#include "portable_log.h"
#include "os_wrapper.h"
#include "../OS/event_bus.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "OTA_MANAGER";

// ============================================================================
// Private State (minimal — no task handles or firmware buffers)
// ============================================================================

static struct {
    bool initialized;
} s_mgr = {0};

// ============================================================================
// MQTT Status Reporting
// ============================================================================

static void report_ota_status(const char *target, const char *status, const char *message)
{
    const char *device_id = serv_mqtt_get_device_id();
    if (!device_id) {
        LOG_W(TAG, "Cannot report status: device ID not available");
        return;
    }

    char topic[128];
    snprintf(topic, sizeof(topic), MQTT_TOPIC_DEVICE_OTA_STATUS_FMT, device_id);

    char payload[256];
    snprintf(payload, sizeof(payload),
             "{\"target\":\"%s\",\"status\":\"%s\",\"message\":\"%s\"}",
             target, status, message);

    serv_mqtt_publish(topic, payload, strlen(payload), 1, false);
    LOG_I(TAG, "[%s] OTA Status: %s - %s", target, status, message);
}

static void report_ota_progress(const char *target, uint8_t progress)
{
    const char *device_id = serv_mqtt_get_device_id();
    if (!device_id) {
        return;
    }

    char topic[128];
    snprintf(topic, sizeof(topic), MQTT_TOPIC_DEVICE_OTA_PROGRESS_FMT, device_id);

    char payload[64];
    snprintf(payload, sizeof(payload), "{\"target\":\"%s\",\"progress\":%u}", target, progress);

    serv_mqtt_publish(topic, payload, strlen(payload), 0, false);
}

// ============================================================================
// Event Bus Handlers — MQTT reporting for OTA events from services
// ============================================================================

static void on_esp32_ota_started(event_type_t type, void *data)
{
    (void)type; (void)data;
    report_ota_status("esp32", "started", "Downloading firmware");
}

static void on_esp32_ota_progress(event_type_t type, void *data)
{
    (void)type;
    if (data) {
        uint8_t progress = *(uint8_t *)data;
        report_ota_progress("esp32", progress);
    }
}

static void on_esp32_ota_completed(event_type_t type, void *data)
{
    (void)type; (void)data;
    report_ota_status("esp32", "completed", "Firmware downloaded and verified");
    report_ota_progress("esp32", 100);
}

static void on_esp32_ota_failed(event_type_t type, void *data)
{
    (void)type; (void)data;
    report_ota_status("esp32", "failed", "Update failed");
}

static void on_stm32_ota_started(event_type_t type, void *data)
{
    (void)type; (void)data;
    report_ota_status("stm32", "started", "Downloading firmware");
}

static void on_stm32_ota_completed(event_type_t type, void *data)
{
    (void)type; (void)data;
    report_ota_status("stm32", "completed", "Firmware verified and transferred");
}

static void on_stm32_ota_failed(event_type_t type, void *data)
{
    (void)type; (void)data;
    report_ota_status("stm32", "failed", "Update failed");
}

// ============================================================================
// Unified JSON Parser & Router
// ============================================================================

static void on_mqtt_ota_notify(event_type_t type, void *data)
{
    (void)type;
    if (!data) {
        return;
    }

    const char *json_payload = (const char *)data;
    LOG_I(TAG, "OTA notification received via MQTT");
    LOG_D(TAG, "Payload: %s", json_payload);

    cJSON *root = cJSON_Parse(json_payload);
    if (!root) {
        LOG_E(TAG, "JSON parse error: %s", cJSON_GetErrorPtr());
        return;
    }

    // Determine target (default to "esp32" for backward compatibility)
    cJSON *target_obj = cJSON_GetObjectItem(root, "target");
    const char *target = "esp32";
    if (cJSON_IsString(target_obj)) {
        target = target_obj->valuestring;
    }

    if (strcmp(target, "esp32") == 0) {
        // ── ESP32 OTA path ──
        cJSON *version_obj = cJSON_GetObjectItem(root, "version");
        cJSON *url_obj = cJSON_GetObjectItem(root, "url");

        if (!cJSON_IsString(version_obj) || !cJSON_IsString(url_obj)) {
            LOG_E(TAG, "ESP32 OTA: missing required fields (version, url)");
            cJSON_Delete(root);
            return;
        }

        cJSON *size_obj = cJSON_GetObjectItem(root, "size");
        cJSON *auto_reboot_obj = cJSON_GetObjectItem(root, "auto_reboot");

        esp32_ota_notification_t notif = {0};
        strncpy(notif.url, url_obj->valuestring, sizeof(notif.url) - 1);
        strncpy(notif.version, version_obj->valuestring, sizeof(notif.version) - 1);
        notif.expected_size = cJSON_IsNumber(size_obj) ? (size_t)size_obj->valueint : 0;
        notif.auto_reboot = cJSON_IsBool(auto_reboot_obj) ? cJSON_IsTrue(auto_reboot_obj) : false;

        LOG_I(TAG, "Routing to ESP32 OTA: version %s", notif.version);
        esp32_ota_status_t status = serv_esp32_ota_trigger(&notif);
        if (status != ESP32_OTA_OK) {
            LOG_E(TAG, "Failed to trigger ESP32 OTA: error %d", status);
        }

    } else if (strcmp(target, "stm32") == 0) {
        // ── STM32 OTA path ──
        cJSON *version_obj = cJSON_GetObjectItem(root, "version");
        cJSON *url_obj = cJSON_GetObjectItem(root, "url");
        cJSON *size_obj = cJSON_GetObjectItem(root, "size");
        cJSON *signature_obj = cJSON_GetObjectItem(root, "signature_rsa");

        if (!cJSON_IsString(version_obj) || !cJSON_IsString(url_obj) ||
            !cJSON_IsNumber(size_obj) || !cJSON_IsString(signature_obj)) {
            LOG_E(TAG, "STM32 OTA: missing required fields (version, url, size, signature_rsa)");
            cJSON_Delete(root);
            return;
        }

        cJSON *sha256_obj = cJSON_GetObjectItem(root, "sha256");
        cJSON *crc32_obj = cJSON_GetObjectItem(root, "crc32");
        cJSON *auto_apply_obj = cJSON_GetObjectItem(root, "auto_apply");

        stm32_ota_notification_t stm32_notif = {0};
        strncpy(stm32_notif.target, target, sizeof(stm32_notif.target) - 1);
        strncpy(stm32_notif.version, version_obj->valuestring, sizeof(stm32_notif.version) - 1);
        strncpy(stm32_notif.url, url_obj->valuestring, sizeof(stm32_notif.url) - 1);
        strncpy(stm32_notif.signature_rsa, signature_obj->valuestring,
                sizeof(stm32_notif.signature_rsa) - 1);
        stm32_notif.size = (uint32_t)size_obj->valueint;

        if (cJSON_IsString(sha256_obj)) {
            strncpy(stm32_notif.sha256, sha256_obj->valuestring, sizeof(stm32_notif.sha256) - 1);
        }
        if (cJSON_IsNumber(crc32_obj)) {
            stm32_notif.crc32 = (uint32_t)crc32_obj->valuedouble;
        }
        stm32_notif.auto_apply = (cJSON_IsBool(auto_apply_obj) && cJSON_IsTrue(auto_apply_obj));

        LOG_I(TAG, "Routing to STM32 OTA: version %s", stm32_notif.version);
        serv_stm32_ota_status_t stm32_status = serv_stm32_ota_trigger(&stm32_notif);
        if (stm32_status != STM32_OTA_OK) {
            LOG_E(TAG, "Failed to trigger STM32 OTA: error %d", stm32_status);
        }

    } else {
        LOG_W(TAG, "Unknown OTA target: %s", target);
    }

    cJSON_Delete(root);
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

    // Initialize ESP32 OTA service
    esp32_ota_status_t esp32_status = serv_esp32_ota_init();
    if (esp32_status != ESP32_OTA_OK) {
        LOG_E(TAG, "Failed to initialize ESP32 OTA service");
        return OTA_MGR_ERR_INTERNAL;
    }

    // Initialize STM32 OTA service
    serv_stm32_ota_status_t stm32_status = serv_stm32_ota_init();
    if (stm32_status != STM32_OTA_OK) {
        LOG_E(TAG, "Failed to initialize STM32 OTA service");
        return OTA_MGR_ERR_INTERNAL;
    }

    s_mgr.initialized = true;
    LOG_I(TAG, "Unified OTA manager initialized (ESP32 + STM32)");

    return OTA_MGR_OK;
}

ota_mgr_status_t cont_ota_manager_start(void)
{
    if (!s_mgr.initialized) {
        LOG_E(TAG, "Cannot start: OTA manager not initialized");
        return OTA_MGR_ERR_NOT_INITIALIZED;
    }

    LOG_I(TAG, "Starting unified OTA manager...");

    // Subscribe to MQTT OTA notification topic
    int msg_id = serv_mqtt_subscribe(MQTT_TOPIC_GLOBAL_OTA_NOTIFY, 1);
    if (msg_id >= 0) {
        LOG_I(TAG, "Subscribed to MQTT topic: %s (msg_id=%d)", MQTT_TOPIC_GLOBAL_OTA_NOTIFY, msg_id);
    } else {
        LOG_W(TAG, "Failed to subscribe to MQTT OTA topic (broker may not be connected yet)");
    }

    // Subscribe to MQTT data for JSON routing
    event_bus_subscribe(EVENT_MQTT_DATA_RECEIVED, on_mqtt_ota_notify);

    // Subscribe to OTA events from services for MQTT status reporting
    event_bus_subscribe(EVENT_OTA_STARTED, on_esp32_ota_started);
    event_bus_subscribe(EVENT_OTA_PROGRESS, on_esp32_ota_progress);
    event_bus_subscribe(EVENT_OTA_COMPLETED, on_esp32_ota_completed);
    event_bus_subscribe(EVENT_OTA_FAILED, on_esp32_ota_failed);
    event_bus_subscribe(EVENT_STM32_OTA_STARTED, on_stm32_ota_started);
    event_bus_subscribe(EVENT_STM32_OTA_COMPLETED, on_stm32_ota_completed);
    event_bus_subscribe(EVENT_STM32_OTA_FAILED, on_stm32_ota_failed);

    LOG_I(TAG, "OTA manager started - listening for ESP32 and STM32 update notifications");
    return OTA_MGR_OK;
}

ota_mgr_status_t cont_ota_cancel_update(void)
{
    if (!s_mgr.initialized) {
        return OTA_MGR_ERR_NOT_INITIALIZED;
    }

    if (serv_esp32_ota_is_in_progress()) {
        serv_esp32_ota_abort();
        LOG_W(TAG, "ESP32 OTA update cancelled");
        report_ota_status("esp32", "cancelled", "Update cancelled by user");
        return OTA_MGR_OK;
    }

    if (serv_stm32_ota_is_in_progress()) {
        serv_stm32_ota_abort();
        LOG_W(TAG, "STM32 OTA update cancelled");
        report_ota_status("stm32", "cancelled", "Update cancelled by user");
        return OTA_MGR_OK;
    }

    return OTA_MGR_OK;
}

void cont_ota_validate_after_boot(void)
{
    if (!s_mgr.initialized) {
        return;
    }

    const esp_partition_t *running = esp_ota_get_running_partition();
    if (!running) {
        LOG_E(TAG, "Failed to get running partition");
        return;
    }

    bool is_ota = (running->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0 ||
                   running->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_1);

    esp32_ota_status_t status = serv_esp32_ota_mark_app_valid();
    if (status == ESP32_OTA_OK) {
        if (is_ota) {
            LOG_I(TAG, "OTA firmware validated after boot");
            report_ota_status("esp32", "validated", "New firmware running successfully");
        } else {
            LOG_I(TAG, "Factory firmware validated (no OTA performed)");
            report_ota_status("esp32", "ready", "Factory firmware active, OTA ready");
        }
    } else {
        LOG_E(TAG, "Failed to mark app as valid (status=%d)", status);
    }
}

bool cont_ota_is_update_in_progress(void)
{
    return serv_esp32_ota_is_in_progress() || serv_stm32_ota_is_in_progress();
}

uint8_t cont_ota_get_progress(void)
{
    return serv_esp32_ota_get_progress();
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
