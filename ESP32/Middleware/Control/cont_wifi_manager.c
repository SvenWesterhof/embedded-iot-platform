/**
 * @file cont_wifi_manager.c
 * @brief WiFi Connection Manager Implementation
 */

#include "cont_wifi_manager.h"
#include "../../OS/event_bus.h"
#include "os_wrapper.h"
#include "portable_log.h"
#include <esp_wifi.h>
#include <esp_netif.h>
#include <esp_event.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <string.h>

static const char *TAG = "WIFI_MGR";

// ============================================================================
// NVS Keys
// ============================================================================

#define NVS_NAMESPACE           "wifi_creds"
#define NVS_KEY_SSID            "ssid"
#define NVS_KEY_PASSWORD        "password"

// ============================================================================
// Event Group Bits
// ============================================================================

#define WIFI_CONNECTED_BIT      BIT0
#define WIFI_FAIL_BIT           BIT1

// ============================================================================
// Internal State
// ============================================================================

typedef struct {
    bool initialized;
    bool started;
    wifi_status_t status;
    wifi_config_params_t config;
    
    // ESP-IDF handles
    esp_netif_t *netif;
    EventGroupHandle_t event_group;
    
    // Connection tracking
    uint8_t retry_count;
    bool auto_reconnect;
    
    // Connection info
    wifi_info_t info;
} wifi_state_t;

static wifi_state_t state = {0};

// ============================================================================
// Internal Functions
// ============================================================================

/**
 * @brief Update connection info from current state
 */
static void update_connection_info(void)
{
    if (!wifi_manager_is_connected()) {
        memset(&state.info, 0, sizeof(wifi_info_t));
        return;
    }
    
    // Get SSID
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        strncpy(state.info.ssid, (char*)ap_info.ssid, WIFI_SSID_MAX_LEN - 1);
        state.info.rssi = ap_info.rssi;
        state.info.channel = ap_info.primary;
    }
    
    // Get IP address
    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(state.netif, &ip_info) == ESP_OK) {
        snprintf(state.info.ip_addr, sizeof(state.info.ip_addr), IPSTR, 
                 IP2STR(&ip_info.ip));
    }
    
    // Get MAC address
    uint8_t mac[6];
    if (esp_wifi_get_mac(WIFI_IF_STA, mac) == ESP_OK) {
        snprintf(state.info.mac_addr, sizeof(state.info.mac_addr),
                 "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
}

/**
 * @brief WiFi event handler
 */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                LOG_I(TAG, "WiFi STA started");
                esp_wifi_connect();
                state.status = WIFI_STATUS_CONNECTING;
                break;
                
            case WIFI_EVENT_STA_CONNECTED:
                LOG_I(TAG, "WiFi connected to AP");
                break;
                
            case WIFI_EVENT_STA_DISCONNECTED: {
                wifi_event_sta_disconnected_t *event = 
                    (wifi_event_sta_disconnected_t*)event_data;
                LOG_W(TAG, "WiFi disconnected (reason: %d)", event->reason);
                
                state.status = WIFI_STATUS_DISCONNECTED;
                xEventGroupClearBits(state.event_group, WIFI_CONNECTED_BIT);
                
                // Publish disconnect event
                event_bus_publish(EVENT_WIFI_DISCONNECTED, NULL);
                
                // Auto-reconnect logic
                if (state.auto_reconnect && state.started) {
                    if (state.config.max_retry == 0 || 
                        state.retry_count < state.config.max_retry) {
                        
                        state.retry_count++;
                        state.status = WIFI_STATUS_RECONNECTING;
                        LOG_I(TAG, "Reconnecting... (attempt %d)", state.retry_count);
                        
                        os_delay_ms(WIFI_RECONNECT_INTERVAL_MS);
                        esp_wifi_connect();
                    } else {
                        LOG_E(TAG, "Max reconnection attempts reached");
                        state.status = WIFI_STATUS_ERROR;
                        xEventGroupSetBits(state.event_group, WIFI_FAIL_BIT);
                    }
                }
                break;
            }
            
            default:
                break;
        }
    } else if (event_base == IP_EVENT) {
        switch (event_id) {
            case IP_EVENT_STA_GOT_IP: {
                ip_event_got_ip_t *event = (ip_event_got_ip_t*)event_data;
                LOG_I(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
                
                state.status = WIFI_STATUS_CONNECTED;
                state.retry_count = 0;
                
                xEventGroupSetBits(state.event_group, WIFI_CONNECTED_BIT);
                
                // Update connection info
                update_connection_info();
                
                // Publish connect event
                event_bus_publish(EVENT_WIFI_CONNECTED, &state.info);
                break;
            }
            
            case IP_EVENT_STA_LOST_IP:
                LOG_W(TAG, "Lost IP address");
                break;
                
            default:
                break;
        }
    }
}

/**
 * @brief Load credentials from NVS
 */
static esp_err_t load_credentials_from_nvs(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        LOG_D(TAG, "No stored credentials found");
        return err;
    }
    
    size_t ssid_len = WIFI_SSID_MAX_LEN;
    size_t pass_len = WIFI_PASSWORD_MAX_LEN;
    
    err = nvs_get_str(nvs, NVS_KEY_SSID, state.config.ssid, &ssid_len);
    if (err != ESP_OK) {
        nvs_close(nvs);
        return err;
    }
    
    err = nvs_get_str(nvs, NVS_KEY_PASSWORD, state.config.password, &pass_len);
    if (err != ESP_OK) {
        nvs_close(nvs);
        return err;
    }
    
    nvs_close(nvs);
    
    LOG_I(TAG, "Loaded credentials for SSID: %s", state.config.ssid);
    return ESP_OK;
}

/**
 * @brief Save credentials to NVS
 */
static esp_err_t save_credentials_to_nvs(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        LOG_E(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return err;
    }
    
    err = nvs_set_str(nvs, NVS_KEY_SSID, state.config.ssid);
    if (err != ESP_OK) {
        nvs_close(nvs);
        return err;
    }
    
    err = nvs_set_str(nvs, NVS_KEY_PASSWORD, state.config.password);
    if (err != ESP_OK) {
        nvs_close(nvs);
        return err;
    }
    
    err = nvs_commit(nvs);
    nvs_close(nvs);
    
    if (err == ESP_OK) {
        LOG_I(TAG, "Credentials saved to NVS");
    }
    
    return err;
}

// ============================================================================
// Public API Implementation
// ============================================================================

wifi_manager_status_t cont_wifi_manager_init(void)
{
    if (state.initialized) {
        LOG_W(TAG, "Already initialized");
        return WIFI_MGR_ERR_ALREADY_INIT;
    }
    
    LOG_I(TAG, "Initializing WiFi manager");
    
    // Create event group
    state.event_group = xEventGroupCreate();
    if (state.event_group == NULL) {
        LOG_E(TAG, "Failed to create event group");
        return WIFI_MGR_ERR_INVALID_STATE;
    }
    
    // Initialize TCP/IP stack
    ESP_ERROR_CHECK(esp_netif_init());
    
    // Create default event loop if not already created
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        LOG_E(TAG, "Failed to create event loop: %s", esp_err_to_name(err));
        return WIFI_MGR_ERR_INVALID_STATE;
    }
    
    // Create default WiFi STA netif
    state.netif = esp_netif_create_default_wifi_sta();
    if (state.netif == NULL) {
        LOG_E(TAG, "Failed to create netif");
        return WIFI_MGR_ERR_INVALID_STATE;
    }
    
    // Initialize WiFi with default config
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        LOG_E(TAG, "Failed to init WiFi: %s", esp_err_to_name(err));
        return WIFI_MGR_ERR_INVALID_STATE;
    }
    
    // Register event handlers
    err = esp_event_handler_instance_register(WIFI_EVENT,
                                               ESP_EVENT_ANY_ID,
                                               &wifi_event_handler,
                                               NULL, NULL);
    if (err != ESP_OK) {
        LOG_E(TAG, "Failed to register WiFi event handler");
        return WIFI_MGR_ERR_INVALID_STATE;
    }
    
    err = esp_event_handler_instance_register(IP_EVENT,
                                               IP_EVENT_STA_GOT_IP,
                                               &wifi_event_handler,
                                               NULL, NULL);
    if (err != ESP_OK) {
        LOG_E(TAG, "Failed to register IP event handler");
        return WIFI_MGR_ERR_INVALID_STATE;
    }
    
    // Set WiFi mode
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        LOG_E(TAG, "Failed to set WiFi mode: %s", esp_err_to_name(err));
        return WIFI_MGR_ERR_INVALID_STATE;
    }
    
    // Set default config
    state.config.auto_reconnect = true;
    state.config.max_retry = WIFI_MAX_RETRY;
    state.auto_reconnect = true;
    
    // Try to load credentials from NVS
    load_credentials_from_nvs();
    
    state.status = WIFI_STATUS_DISCONNECTED;
    state.initialized = true;
    
    LOG_I(TAG, "WiFi manager initialized");
    return WIFI_MGR_OK;
}

wifi_manager_status_t cont_wifi_manager_start(void)
{
    if (!state.initialized) {
        LOG_E(TAG, "Not initialized");
        return WIFI_MGR_ERR_NOT_INITIALIZED;
    }
    
    if (state.started) {
        LOG_W(TAG, "Already started");
        return WIFI_MGR_OK;
    }
    
    LOG_I(TAG, "Starting WiFi manager");
    
    // Check if credentials are configured
    if (strlen(state.config.ssid) == 0) {
        LOG_W(TAG, "No WiFi credentials configured");
        return WIFI_MGR_ERR_INVALID_CREDS;
    }
    
    // Configure WiFi
    wifi_config_t wifi_config = {0};
    strncpy((char*)wifi_config.sta.ssid, state.config.ssid, 
            sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char*)wifi_config.sta.password, state.config.password,
            sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;
    
    // Stability improvements
    wifi_config.sta.listen_interval = 3;  // Listen to every 3 beacons
    wifi_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;  // Scan all channels for better AP discovery
    
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (err != ESP_OK) {
        LOG_E(TAG, "Failed to set WiFi config: %s", esp_err_to_name(err));
        return WIFI_MGR_ERR_INVALID_CREDS;
    }
    
    // Start WiFi
    err = esp_wifi_start();
    if (err != ESP_OK) {
        LOG_E(TAG, "Failed to start WiFi: %s", esp_err_to_name(err));
        return WIFI_MGR_ERR_CONNECT_FAILED;
    }
    
    state.started = true;
    state.retry_count = 0;
    
    LOG_I(TAG, "WiFi manager started, connecting to: %s", state.config.ssid);
    return WIFI_MGR_OK;
}

void cont_wifi_manager_stop(void)
{
    if (!state.started) {
        return;
    }
    
    LOG_I(TAG, "Stopping WiFi manager");
    
    state.started = false;
    state.auto_reconnect = false;  // Prevent reconnection during stop
    
    esp_wifi_disconnect();
    esp_wifi_stop();
    
    state.status = WIFI_STATUS_DISCONNECTED;
    
    LOG_I(TAG, "WiFi manager stopped");
}

wifi_manager_status_t wifi_manager_set_credentials(const char *ssid, 
                                                     const char *password,
                                                     bool save_to_nvs)
{
    if (ssid == NULL) {
        return WIFI_MGR_ERR_INVALID_PARAM;
    }
    
    strncpy(state.config.ssid, ssid, WIFI_SSID_MAX_LEN - 1);
    state.config.ssid[WIFI_SSID_MAX_LEN - 1] = '\0';
    
    if (password != NULL) {
        strncpy(state.config.password, password, WIFI_PASSWORD_MAX_LEN - 1);
        state.config.password[WIFI_PASSWORD_MAX_LEN - 1] = '\0';
    } else {
        state.config.password[0] = '\0';
    }
    
    LOG_I(TAG, "Credentials set for SSID: %s", ssid);
    
    if (save_to_nvs) {
        esp_err_t err = save_credentials_to_nvs();
        return (err == ESP_OK) ? WIFI_MGR_OK : WIFI_MGR_ERR_NVS_FAILED;
    }
    
    return WIFI_MGR_OK;
}

wifi_manager_status_t wifi_manager_connect(void)
{
    if (!state.initialized) {
        return WIFI_MGR_ERR_NOT_INITIALIZED;
    }
    
    if (strlen(state.config.ssid) == 0) {
        LOG_E(TAG, "No SSID configured");
        return WIFI_MGR_ERR_INVALID_CREDS;
    }
    
    state.retry_count = 0;
    state.auto_reconnect = state.config.auto_reconnect;
    
    esp_err_t err = esp_wifi_connect();
    return (err == ESP_OK) ? WIFI_MGR_OK : WIFI_MGR_ERR_CONNECT_FAILED;
}

wifi_manager_status_t wifi_manager_disconnect(void)
{
    state.auto_reconnect = false;  // Prevent auto-reconnect
    esp_err_t err = esp_wifi_disconnect();
    return (err == ESP_OK) ? WIFI_MGR_OK : WIFI_MGR_ERR_INVALID_STATE;
}

bool wifi_manager_is_connected(void)
{
    return state.status == WIFI_STATUS_CONNECTED;
}

wifi_status_t wifi_manager_get_status(void)
{
    return state.status;
}

bool wifi_manager_has_credentials(void)
{
    return strlen(state.config.ssid) > 0;
}

wifi_manager_status_t wifi_manager_get_info(wifi_info_t *info)
{
    if (info == NULL) {
        return WIFI_MGR_ERR_INVALID_PARAM;
    }
    
    if (!wifi_manager_is_connected()) {
        return WIFI_MGR_ERR_NOT_CONNECTED;
    }
    
    update_connection_info();
    memcpy(info, &state.info, sizeof(wifi_info_t));
    return WIFI_MGR_OK;
}

int8_t wifi_manager_get_rssi(void)
{
    if (!wifi_manager_is_connected()) {
        return 0;
    }
    
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        return ap_info.rssi;
    }
    
    return 0;
}

void wifi_manager_set_auto_reconnect(bool enable)
{
    state.auto_reconnect = enable;
    state.config.auto_reconnect = enable;
}

wifi_manager_status_t wifi_manager_clear_credentials(void)
{
    // Clear in-memory credentials
    memset(state.config.ssid, 0, WIFI_SSID_MAX_LEN);
    memset(state.config.password, 0, WIFI_PASSWORD_MAX_LEN);
    
    // Clear NVS
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err == ESP_OK) {
        nvs_erase_all(nvs);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
    
    LOG_I(TAG, "Credentials cleared");
    return WIFI_MGR_OK;
}
