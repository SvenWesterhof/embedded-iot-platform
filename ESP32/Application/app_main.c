#include "app_main.h"
#include "config.h"
#include "credentials.h"
#include "../OS/event_bus.h"
#include "os_wrapper.h"
#include "../Middleware/Control/cont_wifi_manager.h"
#include "../Middleware/Control/cont_ota_manager.h"
#include "../Middleware/Services/serv_ntp_sync.h"
#include "../Middleware/Services/serv_mqtt_client.h"
#include "../Middleware/Features/feat_dashboard_server.h"
#include "../Middleware/Features/feat_stm32_protocol.h"
#include "portable_log.h"
#include <nvs_flash.h>
#include <nvs.h>

static const char *TAG = "APP_MAIN";

// Buffer for device private key loaded from NVS at boot
// Must persist for the lifetime of the MQTT connection
static char s_device_key_pem[2048];

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

/**
 * @brief Handle MQTT connected event - start OTA managers
 */
static void on_mqtt_connected(event_type_t type, void *data)
{
    (void)type;
    (void)data;

    LOG_I(TAG, "MQTT connected - enabling OTA update notifications");

    // Publish online status here (event bus task context) — safe to call serv_mqtt_publish.
    // Must NOT be done from the MQTT event handler (MQTT client task) because calling
    // esp_mqtt_client_publish mid-CONNACK transition produces CLIENT_ERROR on AWS IoT Core.
    char status_topic[96];
    (void)snprintf(status_topic, sizeof(status_topic), "devices/%s/status",
             serv_mqtt_get_device_id() ? serv_mqtt_get_device_id() : "unknown");
    LOG_I(TAG, "Publishing online status to topic: %s", status_topic);
    serv_mqtt_publish_string(status_topic, "online", 1, true);

    // Start unified OTA manager (subscribes to gateway/ota/notify for both ESP32 and STM32)
    ota_mgr_status_t ota_status = cont_ota_manager_start();
    if (ota_status == OTA_MGR_OK) {
        LOG_I(TAG, "OTA manager started - listening for ESP32 and STM32 updates");
    } else {
        LOG_E(TAG, "Failed to start OTA manager: %d", ota_status);
    }
}

// ============================================================================
// Application Lifecycle
// ============================================================================

bool app_init(void)
{
    LOG_I(TAG, "========================================");
    LOG_I(TAG, "   ESP32 Gateway - Initializing");
    LOG_I(TAG, "   Firmware Version 🚀: %s", FIRMWARE_VERSION);
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

    // Subscribe to MQTT events
    event_bus_subscribe(EVENT_MQTT_CONNECTED, on_mqtt_connected);

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
    }
    
    // Load device private key from NVS (provisioned via tools/provision_device.py)
    const char *device_key = NULL;
    {
        nvs_handle_t nvs;
        esp_err_t err = nvs_open("iot_creds", NVS_READONLY, &nvs);
        if (err == ESP_OK) {
            size_t key_len = sizeof(s_device_key_pem) - 1;  // Reserve 1 byte for null terminator
            err = nvs_get_blob(nvs, "device_key", s_device_key_pem, &key_len);
            nvs_close(nvs);
            if (err == ESP_OK) {
                s_device_key_pem[key_len] = '\0';  // mbedTLS PEM parser requires null termination
                device_key = s_device_key_pem;
                LOG_I(TAG, "[OK] Device private key loaded from NVS (%d bytes)", (int)key_len);
            } else {
                LOG_E(TAG, "[FAIL] Device key not found in NVS — run tools/provision_device.py");
            }
        } else {
            LOG_E(TAG, "[FAIL] Failed to open NVS namespace 'iot_creds': %s", esp_err_to_name(err));
        }
    }

    // Initialize MQTT client with credentials
    mqtt_client_config_t mqtt_config = {
        .broker_uri = MQTT_BROKER_URI,
        .device_id = MQTT_TOPIC_PREFIX,
        .client_id = MQTT_CLIENT_ID,
        .username = MQTT_USERNAME,
        .password = MQTT_PASSWORD,
        .keepalive_sec = 120,
        .qos = 1,
        .clean_session = true,
        .tls_ca_cert     = (const char *)amazon_root_ca_pem_start,
        .tls_client_cert = (const char *)device_cert_pem_start,
        .tls_client_key  = device_key,   // NULL if not provisioned → mTLS disabled
    };
    if (serv_mqtt_init(&mqtt_config) == MQTT_OK) {
        LOG_I(TAG, "[OK] MQTT client initialized%s",
              device_key ? " (mTLS)" : " (WARNING: no client key, mTLS disabled)");
    }
    
    // Initialize dashboard server
    if (feat_dashboard_server_init() == DASHBOARD_OK) {
        LOG_I(TAG, "[OK] Dashboard server initialized");
    }
    
    // Initialize STM32 protocol
    if (feat_stm32_protocol_init() == PROTO_OK) {
        LOG_I(TAG, "[OK] STM32 protocol initialized");
    }

    // Initialize unified OTA manager (handles both ESP32 and STM32)
    if (cont_ota_manager_init() == OTA_MGR_OK) {
        char partition_info[64];
        cont_ota_get_partition_info(partition_info, sizeof(partition_info));
        LOG_I(TAG, "[OK] OTA manager initialized (ESP32 + STM32) - Running from: %s", partition_info);
        cont_ota_validate_after_boot();
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
        (void)snprintf(status_json, sizeof(status_json),
            "{\"uptime\":%u,\"wifi\":\"%s\",\"mqtt\":\"%s\",\"ntp\":\"%s\",\"clients\":%d}",
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
            char partition_info[64];
            cont_ota_get_partition_info(partition_info, sizeof(partition_info));
            LOG_I(TAG, "Uptime: %u seconds | WiFi: %s | MQTT: %s | Partition: %s",
                     uptime,
                     wifi_manager_is_connected() ? "Connected" : "Disconnected",
                     serv_mqtt_is_connected() ? "Connected" : "Disconnected",
                     partition_info);
        }
    }
}
