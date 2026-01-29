#ifndef SERVICE_EVENTS_H
#define SERVICE_EVENTS_H

/**
 * @file service_events.h
 * @brief Event data structures for services
 */

#include <stdint.h>

/**
 * @brief Temperature sensor data event payload
 */
typedef struct {
    float temperature;
    float humidity;
    uint8_t sensor_ok;
} temperature_data_t;

#endif // SERVICE_EVENTS_H
