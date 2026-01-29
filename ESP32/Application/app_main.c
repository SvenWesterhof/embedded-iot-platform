#include "app_main.h"
#include "config.h"
#include "credentials.h"
#include "../OS/event_bus.h"
#include "../OS/os_tasks.h"
#include "../OS/os_wrapper.h"
#include "../Middleware/Control/cont_wifi_manager.h"
#include "../Middleware/Services/serv_ntp_sync.h"
#include "../Middleware/Services/serv_mqtt_client.h"
#include "../Middleware/Features/feat_dashboard_server.h"
#include "../Middleware/Features/feat_stm32_protocol.h"
#include "../Drivers_BSP/Custom/portable_log.h"
#include <nvs_flash.h>

static const char *TAG = "APP_MAIN";

// ============================================================================
// Event Handlers
// ============================================================================

/**
 * @brief Handle WiFi connected event - start network services
 */
static void on_wifi_connected(event_type_t type, void *data)
{
    (void)type;
    (void)data;
    
    LOG_I(TAG, "WiFi connected - starting network services");
    
    // Start NTP time sync
    if (serv_ntp_start() == NTP_SYNC_OK) {
        LOG_I(TAG, "NTP sync started");
    }
    
    // Start MQTT client
    if (serv_mqtt_start() == MQTT_OK) {
        LOG_I(TAG, "MQTT client started");
    }
    
    // Start dashboard web server
    if (feat_dashboard_server_start() == DASHBOARD_OK) {
        LOG_I(TAG, "Dashboard server started");
    }
}

/**
 * @brief Handle WiFi disconnected event
 */
static void on_wifi_disconnected(event_type_t type, void *data)
{
    (void)type;
    (void)data;
    
    LOG_W(TAG, "WiFi disconnected - network services may be unavailable");
}

// ============================================================================
// Application Lifecycle
// ============================================================================

bool app_init(void)
{
    LOG_I(TAG, "========================================");
    LOG_I(TAG, "   ESP32 Gateway - Initializing");
    LOG_I(TAG, "========================================");
    
    // Initialize NVS (required for WiFi)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        LOG_W(TAG, "NVS partition was truncated, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    LOG_I(TAG, "[OK] NVS Flash initialized");
    
    // Initialize event bus
    event_bus_init();
    LOG_I(TAG, "[OK] Event bus initialized");
    
    // Subscribe to WiFi events
    event_bus_subscribe(EVENT_WIFI_CONNECTED, on_wifi_connected);
    event_bus_subscribe(EVENT_WIFI_DISCONNECTED, on_wifi_disconnected);
    
    // Initialize OS tasks
    os_tasks_init();
    LOG_I(TAG, "[OK] OS tasks initialized");
    
    // Initialize WiFi manager
    if (cont_wifi_manager_init() == WIFI_MGR_OK) {
        LOG_I(TAG, "[OK] WiFi manager initialized");
        
        // Set WiFi credentials from credentials.h
        wifi_manager_set_credentials(WIFI_SSID, WIFI_PASSWORD, false);
    } else {
        LOG_E(TAG, "[FAIL] WiFi manager init failed");
        return false;
    }
    
    // Initialize NTP service
    if (serv_ntp_init() == NTP_SYNC_OK) {
        LOG_I(TAG, "[OK] NTP service initialized");
        serv_ntp_set_server(NTP_SERVER);
        serv_ntp_set_timezone(NTP_TIMEZONE_OFFSET);
    }
    
    // Initialize MQTT client with credentials
    mqtt_client_config_t mqtt_config = {
        .broker_uri = MQTT_BROKER_URI,
        .client_id = MQTT_CLIENT_ID,
        .username = MQTT_USERNAME,
        .password = MQTT_PASSWORD,
        .topic_prefix = MQTT_TOPIC_PREFIX,
        .keepalive_sec = 120,
        .qos = 1,
        .clean_session = true
    };
    if (serv_mqtt_init(&mqtt_config) == MQTT_OK) {
        LOG_I(TAG, "[OK] MQTT client initialized");
    }
    
    // Initialize dashboard server
    if (feat_dashboard_server_init() == DASHBOARD_OK) {
        LOG_I(TAG, "[OK] Dashboard server initialized");
    }
    
    // Initialize STM32 protocol
    if (feat_stm32_protocol_init() == PROTO_OK) {
        LOG_I(TAG, "[OK] STM32 protocol initialized");
    }
    
    LOG_I(TAG, "========================================");
    LOG_I(TAG, "   Application initialized");
    LOG_I(TAG, "========================================");
    
    return true;
}

void app_run(void)
{
    LOG_I(TAG, "Starting application...");
    
    // Start WiFi connection (triggers on_wifi_connected when done)
    if (cont_wifi_manager_start() == WIFI_MGR_OK) {
        LOG_I(TAG, "WiFi manager started - connecting...");
    } else {
        LOG_W(TAG, "WiFi start failed - check credentials");
    }
    
    // Start STM32 protocol
    feat_stm32_protocol_start();
    
    // Main application loop - broadcast status to dashboard
    uint32_t uptime = 0;
    char status_json[256];
    
    while (1) {
        os_delay_ms(2000);  // 2 second update
        uptime += 2;
        
        // Build status JSON for dashboard
        snprintf(status_json, sizeof(status_json),
            "{\"uptime\":%lu,\"wifi\":\"%s\",\"mqtt\":\"%s\",\"ntp\":\"%s\",\"clients\":%d}",
            uptime,
            wifi_manager_is_connected() ? "Connected" : "Disconnected",
            serv_mqtt_is_connected() ? "Connected" : "Disconnected",
            serv_ntp_is_valid() ? "Synced" : "Not synced",
            dashboard_get_client_count()
        );
        
        // Broadcast to all WebSocket clients
        dashboard_broadcast_json(status_json);
        
        // Log every 30 seconds
        if (uptime % 30 == 0) {
            LOG_I(TAG, "Uptime: %lu seconds | WiFi: %s | MQTT: %s", 
                     uptime,
                     wifi_manager_is_connected() ? "Connected" : "Disconnected",
                     serv_mqtt_is_connected() ? "Connected" : "Disconnected");
        }
    }
}
