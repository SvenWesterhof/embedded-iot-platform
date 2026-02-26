#ifndef EVENT_BUS_H
#define EVENT_BUS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Event types
typedef enum {
    EVENT_NONE = 0,
    
    // STM32 Protocol Events
    EVENT_STM32_PACKET_RECEIVED,
    EVENT_STM32_DATA_READY,
    EVENT_STM32_MEASUREMENT_COMPLETE,
    EVENT_STM32_ERROR,
    EVENT_STM32_ACK_RECEIVED,
    EVENT_STM32_TIMEOUT,
    
    // Dashboard Events
    EVENT_DASHBOARD_CONNECTED,
    EVENT_DASHBOARD_DISCONNECTED,
    EVENT_DASHBOARD_REQUEST_HISTORY,
    EVENT_DASHBOARD_START_MEASUREMENT,
    EVENT_DASHBOARD_STOP_MEASUREMENT,
    EVENT_DASHBOARD_HEARTBEAT,
    
    // WiFi Events
    EVENT_WIFI_CONNECTED,
    EVENT_WIFI_DISCONNECTED,
    
    // NTP Events
    EVENT_NTP_TIME_SYNCED,
    EVENT_NTP_SYNC_FAILED,
    
    // MQTT Events
    EVENT_MQTT_CONNECTED,
    EVENT_MQTT_DISCONNECTED,
    EVENT_MQTT_DATA_RECEIVED,
    EVENT_MQTT_ERROR,
    
    // Sleep Events
    EVENT_SLEEP_STATE_CHANGED,
    
    // System Events
    EVENT_SYSTEM_ERROR,

    // OTA Events (ESP32)
    EVENT_OTA_NOTIFICATION,        // MQTT notification parsed for ESP32 OTA
    EVENT_OTA_STARTED,
    EVENT_OTA_PROGRESS,
    EVENT_OTA_COMPLETED,
    EVENT_OTA_FAILED,
    EVENT_OTA_PENDING_VALIDATION,
    EVENT_OTA_VALIDATED,
    EVENT_OTA_ROLLBACK,

    // STM32 OTA Events
    EVENT_STM32_OTA_NOTIFICATION,  // MQTT notification parsed for STM32 OTA
    EVENT_STM32_OTA_STARTED,
    EVENT_STM32_OTA_PROGRESS,
    EVENT_STM32_OTA_COMPLETED,
    EVENT_STM32_OTA_FAILED,

    EVENT_MAX
} event_type_t;

// Event callback function type
typedef void (*event_callback_t)(event_type_t type, void *data);

/**
 * @brief Initialize the event bus
 * @return true if successful
 */
bool event_bus_init(void);

/**
 * @brief Subscribe to an event
 * @param type Event type to subscribe to
 * @param callback Callback function to call when event occurs
 * @return true if successful
 */
bool event_bus_subscribe(event_type_t type, event_callback_t callback);

/**
 * @brief Unsubscribe from an event
 * @param type Event type to unsubscribe from
 * @param callback Callback function to remove
 * @return true if successful
 */
bool event_bus_unsubscribe(event_type_t type, event_callback_t callback);

/**
 * @brief Publish an event (caller must ensure data remains valid until dispatched)
 * @param type Event type to publish
 * @param data Optional event data (pointer stored as-is, not copied)
 * @return true if successful
 */
bool event_bus_publish(event_type_t type, void *data);

/**
 * @brief Publish an event with a heap-copied data payload
 *
 * Use this instead of event_bus_publish() when the data pointer is transient
 * (stack variable, callback parameter, or buffer that will be overwritten).
 * The event bus takes ownership of the copy and frees it after dispatch.
 *
 * @param type Event type to publish
 * @param data Pointer to data to copy (can be NULL if data_size is 0)
 * @param data_size Size of data in bytes
 * @return true if successful
 */
bool event_bus_publish_copy(event_type_t type, const void *data, size_t data_size);

#endif // EVENT_BUS_H
