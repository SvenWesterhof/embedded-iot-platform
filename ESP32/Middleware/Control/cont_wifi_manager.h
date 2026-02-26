/**
 * @file cont_wifi_manager.h
 * @brief WiFi Connection Manager Controller
 * 
 * Controls WiFi connectivity for the ESP32 Gateway:
 * - Connect to configured WiFi network
 * - Handle reconnection on disconnect
 * - Store WiFi credentials in NVS
 * - Publish WiFi events to event bus
 */

#ifndef CONT_WIFI_MANAGER_H
#define CONT_WIFI_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include <esp_err.h>

// ============================================================================
// Module-Specific Error Codes
// ============================================================================

/**
 * @brief WiFi Manager status codes
 * 
 * Module-specific error codes for decoupled error handling.
 * Negative values indicate errors, zero indicates success.
 */
typedef enum {
    WIFI_MGR_OK = 0,                        /**< Success */
    WIFI_MGR_ERR_NOT_INITIALIZED = -1,      /**< Manager not initialized */
    WIFI_MGR_ERR_ALREADY_INIT = -2,         /**< Already initialized */
    WIFI_MGR_ERR_INVALID_PARAM = -3,        /**< Invalid parameter */
    WIFI_MGR_ERR_NOT_CONNECTED = -4,        /**< Not connected to WiFi */
    WIFI_MGR_ERR_CONNECT_FAILED = -5,       /**< Connection failed */
    WIFI_MGR_ERR_INVALID_CREDS = -6,        /**< Invalid credentials */
    WIFI_MGR_ERR_NVS_FAILED = -7,           /**< NVS operation failed */
    WIFI_MGR_ERR_TIMEOUT = -8,              /**< Operation timeout */
    WIFI_MGR_ERR_INVALID_STATE = -9,        /**< Invalid state for operation */
} wifi_manager_status_t;

// ============================================================================
// Constants
// ============================================================================

#define WIFI_SSID_MAX_LEN           32
#define WIFI_PASSWORD_MAX_LEN       64
#define WIFI_RECONNECT_INTERVAL_MS  5000
#define WIFI_MAX_RETRY              5

// ============================================================================
// Data Types
// ============================================================================

/**
 * @brief WiFi connection status
 */
typedef enum {
    WIFI_STATUS_DISCONNECTED = 0,
    WIFI_STATUS_CONNECTING,
    WIFI_STATUS_CONNECTED,
    WIFI_STATUS_RECONNECTING,
    WIFI_STATUS_ERROR,
} wifi_status_t;

/**
 * @brief WiFi configuration structure
 */
typedef struct {
    char ssid[WIFI_SSID_MAX_LEN];           /**< WiFi SSID */
    char password[WIFI_PASSWORD_MAX_LEN];   /**< WiFi password */
    bool auto_reconnect;                    /**< Auto-reconnect on disconnect */
    uint8_t max_retry;                      /**< Max reconnection attempts (0 = infinite) */
} wifi_config_params_t;

/**
 * @brief WiFi connection info
 */
typedef struct {
    char ssid[WIFI_SSID_MAX_LEN];           /**< Connected SSID */
    uint8_t rssi;                           /**< Signal strength (dBm) */
    uint8_t channel;                        /**< WiFi channel */
    char ip_addr[16];                       /**< IP address string */
    char mac_addr[18];                      /**< MAC address string */
} wifi_info_t;

// ============================================================================
// Public API
// ============================================================================

/**
 * @brief Initialize WiFi manager
 * 
 * Sets up WiFi driver and netif. Does not connect yet.
 * 
 * @return WIFI_MGR_OK on success, error code otherwise
 */
wifi_manager_status_t cont_wifi_manager_init(void);

/**
 * @brief Start WiFi manager
 * 
 * Starts WiFi and attempts connection if credentials are configured.
 * 
 * @return WIFI_MGR_OK on success, error code otherwise
 */
wifi_manager_status_t cont_wifi_manager_start(void);

/**
 * @brief Stop WiFi manager
 * 
 * Disconnects from WiFi and stops the manager.
 */
void cont_wifi_manager_stop(void);

/**
 * @brief Configure WiFi credentials
 * 
 * Sets WiFi credentials and saves to NVS if save_to_nvs is true.
 * 
 * @param ssid WiFi SSID
 * @param password WiFi password
 * @param save_to_nvs Save credentials to NVS for persistence
 * @return WIFI_MGR_OK on success, error code otherwise
 */
wifi_manager_status_t wifi_manager_set_credentials(const char *ssid, 
                                                     const char *password,
                                                     bool save_to_nvs);

/**
 * @brief Connect to WiFi
 * 
 * Initiates connection to configured WiFi network.
 * Returns immediately; connection status delivered via events.
 * 
 * @return WIFI_MGR_OK if connection initiated, error code otherwise
 */
wifi_manager_status_t wifi_manager_connect(void);

/**
 * @brief Disconnect from WiFi
 * 
 * Gracefully disconnects from current WiFi network.
 * 
 * @return WIFI_MGR_OK on success
 */
wifi_manager_status_t wifi_manager_disconnect(void);

/**
 * @brief Check if WiFi is connected
 * 
 * @return true if connected to WiFi, false otherwise
 */
bool wifi_manager_is_connected(void);

/**
 * @brief Get WiFi connection status
 * 
 * @return Current WiFi status
 */
wifi_status_t wifi_manager_get_status(void);

/**
 * @brief Get WiFi connection info
 * 
 * @param info Pointer to structure to fill with connection info
 * @return WIFI_MGR_OK on success, WIFI_MGR_ERR_NOT_CONNECTED if not connected
 */
wifi_manager_status_t wifi_manager_get_info(wifi_info_t *info);

/**
 * @brief Check if WiFi credentials are configured (in memory, loaded from NVS or set via API)
 *
 * @return true if SSID is configured, false otherwise
 */
bool wifi_manager_has_credentials(void);

/**
 * @brief Get WiFi signal strength (RSSI)
 *
 * @return RSSI in dBm, or 0 if not connected
 */
int8_t wifi_manager_get_rssi(void);

/**
 * @brief Enable/disable auto-reconnect
 * 
 * @param enable true to enable auto-reconnect, false to disable
 */
void wifi_manager_set_auto_reconnect(bool enable);

/**
 * @brief Clear stored WiFi credentials from NVS
 * 
 * @return WIFI_MGR_OK on success
 */
wifi_manager_status_t wifi_manager_clear_credentials(void);

#endif // CONT_WIFI_MANAGER_H
