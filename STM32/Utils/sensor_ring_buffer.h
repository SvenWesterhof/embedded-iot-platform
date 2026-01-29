/**
 * @file sensor_ring_buffer.h
 * @brief Generic ring buffer for storing sensor samples with timestamps
 *
 * This module provides a reusable, thread-safe ring buffer for storing sensor
 * samples. Multiple instances can be created for different sensor types.
 *
 * Usage example:
 * @code
 * // Create a buffer for temperature samples
 * sensor_ring_buffer_t temp_buffer;
 * sensor_ring_buffer_config_t config = sensor_ring_buffer_get_default_config();
 * config.sensor_type = SENSOR_TEMPERATURE;
 * sensor_ring_buffer_init(&temp_buffer, &config);
 *
 * // Add samples
 * sensor_sample_t sample = { .sensor_type = SENSOR_TEMPERATURE, .timestamp = 12345, .value = 2350 };
 * sensor_ring_buffer_push(&temp_buffer, &sample);
 *
 * // Read samples
 * sensor_sample_t samples[10];
 * uint32_t count;
 * sensor_ring_buffer_read(&temp_buffer, 0, samples, 10, &count);
 * @endcode
 */

#ifndef SENSOR_RING_BUFFER_H
#define SENSOR_RING_BUFFER_H

#include <stdint.h>
#include <stdbool.h>
#include "os_wrapper.h"
#include "protocol_common.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Configuration
// ============================================================================

/**
 * @brief Default buffer capacity (number of samples)
 *
 * With sensor_sample_t being 9 bytes, 455 samples use ~4KB
 */
#define SENSOR_RING_BUFFER_DEFAULT_CAPACITY    455

// ============================================================================
// Types
// ============================================================================

/**
 * @brief Ring buffer status codes
 */
typedef enum {
    SENSOR_RING_BUFFER_OK = 0,
    SENSOR_RING_BUFFER_ERR_INVALID_ARG,
    SENSOR_RING_BUFFER_ERR_NOT_INIT,
    SENSOR_RING_BUFFER_ERR_ALREADY_INIT,
    SENSOR_RING_BUFFER_ERR_EMPTY,
    SENSOR_RING_BUFFER_ERR_NO_MEM,
} sensor_ring_buffer_status_t;

/**
 * @brief Ring buffer configuration
 */
typedef struct {
    uint32_t capacity;          /**< Maximum number of samples (0 = default) */
    sensor_type_t sensor_type;  /**< Sensor type for samples in this buffer */
} sensor_ring_buffer_config_t;

/**
 * @brief Ring buffer instance
 *
 * Allocate one of these per sensor type you want to buffer.
 * All fields are private - use API functions to access.
 */
typedef struct {
    sensor_sample_t *buffer;    /**< Sample storage (dynamically allocated) */
    uint32_t capacity;          /**< Maximum samples */
    uint32_t head;              /**< Write index */
    uint32_t tail;              /**< Read index (oldest sample) */
    uint32_t count;             /**< Current sample count */
    sensor_type_t sensor_type;  /**< Sensor type for this buffer */
    os_mutex_handle_t mutex;    /**< Thread safety mutex */
    bool initialized;           /**< Initialization flag */
} sensor_ring_buffer_t;

// ============================================================================
// Public API
// ============================================================================

/**
 * @brief Get default configuration
 * @return Default configuration with default capacity
 */
sensor_ring_buffer_config_t sensor_ring_buffer_get_default_config(void);

/**
 * @brief Initialize a ring buffer instance
 *
 * Allocates memory for the buffer and initializes synchronization primitives.
 *
 * @param rb Pointer to ring buffer instance (caller allocates the struct)
 * @param config Configuration (NULL for defaults)
 * @return SENSOR_RING_BUFFER_OK on success
 */
sensor_ring_buffer_status_t sensor_ring_buffer_init(
    sensor_ring_buffer_t *rb,
    const sensor_ring_buffer_config_t *config);

/**
 * @brief Deinitialize a ring buffer instance
 *
 * Frees allocated memory and resources.
 *
 * @param rb Pointer to ring buffer instance
 * @return SENSOR_RING_BUFFER_OK on success
 */
sensor_ring_buffer_status_t sensor_ring_buffer_deinit(sensor_ring_buffer_t *rb);

/**
 * @brief Check if ring buffer is initialized
 *
 * @param rb Pointer to ring buffer instance
 * @return true if initialized
 */
bool sensor_ring_buffer_is_initialized(const sensor_ring_buffer_t *rb);

/**
 * @brief Push a sample into the buffer
 *
 * If buffer is full, oldest sample is overwritten (circular behavior).
 * Thread-safe.
 *
 * @param rb Pointer to ring buffer instance
 * @param sample Sample to add
 * @return SENSOR_RING_BUFFER_OK on success
 */
sensor_ring_buffer_status_t sensor_ring_buffer_push(
    sensor_ring_buffer_t *rb,
    const sensor_sample_t *sample);

/**
 * @brief Get the number of samples in the buffer
 *
 * @param rb Pointer to ring buffer instance
 * @return Number of samples currently stored, 0 if rb is NULL or not initialized
 */
uint32_t sensor_ring_buffer_get_count(const sensor_ring_buffer_t *rb);

/**
 * @brief Get the buffer capacity
 *
 * @param rb Pointer to ring buffer instance
 * @return Maximum number of samples the buffer can hold
 */
uint32_t sensor_ring_buffer_get_capacity(const sensor_ring_buffer_t *rb);

/**
 * @brief Read samples from the buffer (non-destructive)
 *
 * Reads samples starting from start_index (0 = oldest sample).
 * Samples remain in the buffer after reading.
 * Thread-safe.
 *
 * @param rb Pointer to ring buffer instance
 * @param start_index Starting index (0 = oldest sample)
 * @param samples Output buffer for samples
 * @param max_samples Maximum samples to read
 * @param samples_read Output: actual number of samples read
 * @return SENSOR_RING_BUFFER_OK on success, SENSOR_RING_BUFFER_ERR_EMPTY if no samples
 */
sensor_ring_buffer_status_t sensor_ring_buffer_read(
    sensor_ring_buffer_t *rb,
    uint32_t start_index,
    sensor_sample_t *samples,
    uint32_t max_samples,
    uint32_t *samples_read);

/**
 * @brief Peek at a single sample by index (non-destructive)
 *
 * @param rb Pointer to ring buffer instance
 * @param index Sample index (0 = oldest)
 * @param sample Output: sample at index
 * @return SENSOR_RING_BUFFER_OK on success, SENSOR_RING_BUFFER_ERR_INVALID_ARG if index out of range
 */
sensor_ring_buffer_status_t sensor_ring_buffer_peek(
    sensor_ring_buffer_t *rb,
    uint32_t index,
    sensor_sample_t *sample);

/**
 * @brief Clear all samples from the buffer
 *
 * Thread-safe.
 *
 * @param rb Pointer to ring buffer instance
 * @return SENSOR_RING_BUFFER_OK on success
 */
sensor_ring_buffer_status_t sensor_ring_buffer_clear(sensor_ring_buffer_t *rb);

/**
 * @brief Get the sensor type for this buffer
 *
 * @param rb Pointer to ring buffer instance
 * @return Sensor type, or 0 if rb is NULL
 */
sensor_type_t sensor_ring_buffer_get_sensor_type(const sensor_ring_buffer_t *rb);

#ifdef __cplusplus
}
#endif

#endif // SENSOR_RING_BUFFER_H
