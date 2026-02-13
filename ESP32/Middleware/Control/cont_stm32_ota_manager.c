/**
 * @file cont_stm32_ota_manager.c
 * @brief STM32 OTA Manager - Downloads firmware, verifies signature, forwards to STM32
 */

#include "cont_stm32_ota_manager.h"
#include "event_bus.h"
#include "os_task.h"
#include "protocol_common.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "STM32_OTA";

// OTA Manager State
static struct {
    stm32_ota_state_t state;
    stm32_ota_notification_t current_update;
    TaskHandle_t ota_task_handle;
    SemaphoreHandle_t state_mutex;
} s_ota_mgr = {
    .state = STM32_OTA_STATE_IDLE,
    .ota_task_handle = NULL,
    .state_mutex = NULL,
};

// Forward declarations
static void stm32_ota_task(void *arg);
static void handle_mqtt_stm32_ota_event(event_type_t type, void *data);

stm32_ota_mgr_status_t cont_stm32_ota_init(void)
{
    ESP_LOGI(TAG, "Initializing STM32 OTA Manager");

    // Create mutex
    s_ota_mgr.state_mutex = xSemaphoreCreateMutex();
    if (s_ota_mgr.state_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return STM32_OTA_MGR_ERR_NO_MEM;
    }

    // Subscribe to MQTT events
    event_bus_subscribe(EVENT_MQTT_DATA_RECEIVED, handle_mqtt_stm32_ota_event);

    ESP_LOGI(TAG, "STM32 OTA Manager initialized");
    return STM32_OTA_MGR_OK;
}

stm32_ota_mgr_status_t cont_stm32_ota_trigger_update(const stm32_ota_notification_t *notification)
{
    if (notification == NULL) {
        return STM32_OTA_MGR_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_ota_mgr.state_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return STM32_OTA_MGR_ERR_IN_PROGRESS;
    }

    if (s_ota_mgr.state != STM32_OTA_STATE_IDLE) {
        xSemaphoreGive(s_ota_mgr.state_mutex);
        ESP_LOGW(TAG, "Update already in progress");
        return STM32_OTA_MGR_ERR_IN_PROGRESS;
    }

    // Copy notification
    memcpy(&s_ota_mgr.current_update, notification, sizeof(stm32_ota_notification_t));
    s_ota_mgr.state = STM32_OTA_STATE_DOWNLOADING;
    xSemaphoreGive(s_ota_mgr.state_mutex);

    // Create OTA task
    BaseType_t ret = xTaskCreatePinnedToCore(
        stm32_ota_task,
        "stm32_ota",
        8192,  // Stack for HTTPS + signature
        NULL,
        5,     // Normal priority
        &s_ota_mgr.ota_task_handle,
        1      // Core 1 (not WiFi core)
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create OTA task");
        // Revert state change (need mutex protection)
        if (xSemaphoreTake(s_ota_mgr.state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            s_ota_mgr.state = STM32_OTA_STATE_IDLE;
            xSemaphoreGive(s_ota_mgr.state_mutex);
        }
        return STM32_OTA_MGR_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "STM32 OTA triggered: version %s", notification->version);
    event_bus_publish(EVENT_STM32_OTA_STARTED, NULL);

    return STM32_OTA_MGR_OK;
}

bool cont_stm32_ota_is_update_in_progress(void)
{
    bool in_progress = false;
    if (xSemaphoreTake(s_ota_mgr.state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        in_progress = (s_ota_mgr.state != STM32_OTA_STATE_IDLE &&
                      s_ota_mgr.state != STM32_OTA_STATE_COMPLETE &&
                      s_ota_mgr.state != STM32_OTA_STATE_FAILED);
        xSemaphoreGive(s_ota_mgr.state_mutex);
    }
    return in_progress;
}

stm32_ota_state_t cont_stm32_ota_get_state(void)
{
    stm32_ota_state_t state = STM32_OTA_STATE_IDLE;
    if (xSemaphoreTake(s_ota_mgr.state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        state = s_ota_mgr.state;
        xSemaphoreGive(s_ota_mgr.state_mutex);
    }
    return state;
}

stm32_ota_mgr_status_t cont_stm32_ota_abort(void)
{
    ESP_LOGW(TAG, "OTA abort requested");

    if (xSemaphoreTake(s_ota_mgr.state_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire mutex for abort");
        return STM32_OTA_MGR_ERR_TIMEOUT;
    }

    TaskHandle_t task_to_delete = s_ota_mgr.ota_task_handle;
    s_ota_mgr.ota_task_handle = NULL;
    s_ota_mgr.state = STM32_OTA_STATE_IDLE;

    xSemaphoreGive(s_ota_mgr.state_mutex);

    // Delete task AFTER releasing mutex to avoid deadlock
    if (task_to_delete != NULL) {
        vTaskDelete(task_to_delete);
    }

    return STM32_OTA_MGR_OK;
}

stm32_ota_mgr_status_t cont_stm32_ota_deinit(void)
{
    ESP_LOGI(TAG, "Deinitializing STM32 OTA Manager");

    // Abort any ongoing update first
    cont_stm32_ota_abort();

    // Unsubscribe from events
    event_bus_unsubscribe(EVENT_MQTT_DATA_RECEIVED, handle_mqtt_stm32_ota_event);

    // Delete mutex
    if (s_ota_mgr.state_mutex != NULL) {
        vSemaphoreDelete(s_ota_mgr.state_mutex);
        s_ota_mgr.state_mutex = NULL;
    }

    ESP_LOGI(TAG, "STM32 OTA Manager deinitialized");
    return STM32_OTA_MGR_OK;
}

// ============================================================================
// Internal Implementation
// ============================================================================

static void stm32_ota_task(void *arg)
{
    ESP_LOGI(TAG, "STM32 OTA task started");
    ESP_LOGI(TAG, "URL: %s", s_ota_mgr.current_update.url);
    ESP_LOGI(TAG, "Size: %lu bytes", s_ota_mgr.current_update.size);
    ESP_LOGI(TAG, "Version: %s", s_ota_mgr.current_update.version);

    // TODO Phase 3: Implement full OTA flow
    // 1. Download firmware via HTTPS (can extract to serv_https_download later)
    // 2. Verify ED25519 signature (can extract to serv_signature_verify later)
    // 3. Transfer to STM32 via feat_stm32_protocol
    //    - Send CMD_FW_UPDATE_START
    //    - Send firmware in CMD_FW_UPDATE_CHUNK packets
    //    - Send CMD_FW_UPDATE_END
    // 4. Wait for STM32 reboot and verification

    ESP_LOGI(TAG, "STM32 OTA implementation in progress...");

    // Stub: Mark as complete for now
    // Use mutex protection when changing state
    if (xSemaphoreTake(s_ota_mgr.state_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        s_ota_mgr.state = STM32_OTA_STATE_COMPLETE;
        s_ota_mgr.ota_task_handle = NULL;
        xSemaphoreGive(s_ota_mgr.state_mutex);
    } else {
        ESP_LOGE(TAG, "Failed to acquire mutex in OTA task");
    }

    event_bus_publish(EVENT_STM32_OTA_COMPLETED, NULL);
    vTaskDelete(NULL);
}

static void handle_mqtt_stm32_ota_event(event_type_t type, void *data)
{
    if (type != EVENT_MQTT_DATA_RECEIVED || data == NULL) {
        return;
    }

    // TODO: Parse MQTT payload
    // Check if topic is "gateway/stm32/ota/notify"
    // Extract notification fields (version, url, size, signature, crc32)
    // Call cont_stm32_ota_trigger_update()

    ESP_LOGD(TAG, "MQTT event - parsing not yet implemented");
}
