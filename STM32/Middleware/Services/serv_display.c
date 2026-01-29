#include "serv_display.h"
#include "event_bus.h"
#include "service_events.h"
#include "ips_display.h"
#include <stddef.h>

/**
 * @brief Event handler for temperature updates
 */
static void on_temperature_updated(event_t* event)
{
    if (event->data != NULL) {
        temperature_data_t* temp_data = (temperature_data_t*)event->data;
        
        // Update display with new temperature data
        ips_display_write_temp_data(temp_data->temperature, temp_data->humidity);
    }
}

/**
 * @brief Event handler for sensor errors
 */
static void on_sensor_error(event_t* event)
{
    // Handle sensor error - display zeros or error indication
    ips_display_write_temp_data(0.0f, 0.0f);
}

void display_init(void)
{
    // Initialize display hardware
    ips_display_init();
    
    // Subscribe to relevant events
    event_bus_subscribe(EVENT_TEMPERATURE_UPDATED, on_temperature_updated);
    event_bus_subscribe(EVENT_SENSOR_ERROR, on_sensor_error);
}

void display_run(void)
{
    // Display service is event-driven, no polling needed
    // All updates happen via event callbacks
}
