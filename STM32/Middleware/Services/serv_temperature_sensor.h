#ifndef SERV_TEMPERATURE_SENSOR_H
#define SERV_TEMPERATURE_SENSOR_H

#include <stdint.h>
#include <stdbool.h>
#include "protocol_common.h"

// ============================================================================
// Configuration
// ============================================================================

/**
 * @brief Default interval for storing samples to buffer (10 seconds)
 */
#define TEMP_SENSOR_BUFFER_INTERVAL_MS    10000

// ============================================================================
// Types
// ============================================================================

/**
 * @brief Callback type for getting Unix timestamp
 *
 * When RTC is available, set this to return Unix time.
 * If not set, uses os_get_time_ms() (milliseconds since boot).
 *
 * @return Timestamp value
 */
typedef uint32_t (*temp_sensor_timestamp_fn)(void);

// ============================================================================
// Core API
// ============================================================================

/**
 * @brief Initialize the temperature sensor service
 */
void temperature_sensor_init(void);

/**
 * @brief Run the temperature sensor service (call periodically)
 *
 * Reads the sensor and publishes events. Also stores samples
 * to the buffer at the configured interval.
 */
void temperature_sensor_run(void);

// ============================================================================
// Buffer API
// ============================================================================

/**
 * @brief Get number of buffered temperature samples
 * @return Number of samples in buffer
 */
uint32_t temperature_sensor_buffer_get_count(void);

/**
 * @brief Read temperature samples from buffer
 *
 * Reads samples starting from start_index. Does not remove samples.
 *
 * @param start_index Starting index (0 = oldest)
 * @param samples Output buffer for samples
 * @param max_samples Maximum samples to read
 * @param samples_read Output: actual number read
 * @return true on success, false on error or empty
 */
bool temperature_sensor_buffer_read(
    uint32_t start_index,
    sensor_sample_t *samples,
    uint32_t max_samples,
    uint32_t *samples_read);

/**
 * @brief Clear all buffered temperature samples
 */
void temperature_sensor_buffer_clear(void);

/**
 * @brief Set the timestamp callback function
 *
 * Call this when RTC becomes available to switch to Unix timestamps.
 *
 * @param get_timestamp Timestamp function (NULL to use os_get_time_ms)
 */
void temperature_sensor_set_timestamp_fn(temp_sensor_timestamp_fn get_timestamp);

#endif // SERV_TEMPERATURE_SENSOR_H
