#ifndef EVENT_BUS_H
#define EVENT_BUS_H

/**
 * @file event_bus.h
 * @brief Event Bus for bottom-up communication between layers
 * 
 * This module implements a simple event bus/pub-sub pattern to allow
 * lower layers to notify upper layers without direct coupling.
 * 
 * Example usage:
 * - Driver detects button press → publishes EVENT_BUTTON_PRESSED
 * - Application subscribes to EVENT_BUTTON_PRESSED and handles it
 */

#include <stdint.h>
#include <stdbool.h>

// Event Types
typedef enum {
    EVENT_NONE = 0,
    EVENT_BUTTON_PRESSED,
    EVENT_TEMPERATURE_UPDATED,
    EVENT_SENSOR_ERROR,
    EVENT_DISPLAY_READY,
    EVENT_USER_DEFINED_START = 100  // User can define events starting from 100
} event_type_t;

// Event Data Structure
typedef struct {
    event_type_t type;
    void* data;         // Pointer to event-specific data
    uint32_t data_size; // Size of data in bytes
    uint32_t timestamp; // Timestamp when event was created
} event_t;

// Event Callback Function Type
typedef void (*event_callback_t)(event_t* event);

// Maximum number of subscribers per event type
#define MAX_SUBSCRIBERS_PER_EVENT   5

// Event Bus Statistics
typedef struct {
    uint32_t publish_success_count;    // Total successful publishes
    uint32_t publish_fail_count;       // Total failed publishes
    uint32_t queue_overflow_count;     // Queue full errors
    uint32_t data_too_large_count;     // Data size exceeded
    uint32_t process_count;            // Total events processed
    uint32_t max_queue_depth;          // Maximum queue depth reached
} event_bus_stats_t;

// Event Bus Functions
void event_bus_init(void);
bool event_bus_subscribe(event_type_t event_type, event_callback_t callback);
bool event_bus_unsubscribe(event_type_t event_type, event_callback_t callback);
bool event_bus_publish(event_type_t event_type, void* data, uint32_t data_size);
void event_bus_process(void);

// Diagnostics Functions
event_bus_stats_t event_bus_get_stats(void);
void event_bus_reset_stats(void);
uint8_t event_bus_get_queue_depth(void);

// Helper function to get current tick (for timestamp)
uint32_t event_bus_get_tick(void);

#endif // EVENT_BUS_H
