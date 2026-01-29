/**
 * @file sensor_ring_buffer.c
 * @brief Generic ring buffer implementation for sensor samples
 */

#include "sensor_ring_buffer.h"
#include <stdlib.h>
#include <string.h>

// ============================================================================
// Public API Implementation
// ============================================================================

sensor_ring_buffer_config_t sensor_ring_buffer_get_default_config(void)
{
    sensor_ring_buffer_config_t config = {
        .capacity = SENSOR_RING_BUFFER_DEFAULT_CAPACITY,
        .sensor_type = SENSOR_TEMPERATURE,
    };
    return config;
}

sensor_ring_buffer_status_t sensor_ring_buffer_init(
    sensor_ring_buffer_t *rb,
    const sensor_ring_buffer_config_t *config)
{
    if (rb == NULL) {
        return SENSOR_RING_BUFFER_ERR_INVALID_ARG;
    }

    if (rb->initialized) {
        return SENSOR_RING_BUFFER_ERR_ALREADY_INIT;
    }

    // Use defaults if config is NULL
    sensor_ring_buffer_config_t cfg;
    if (config != NULL) {
        cfg = *config;
    } else {
        cfg = sensor_ring_buffer_get_default_config();
    }

    // Use default capacity if not specified
    if (cfg.capacity == 0) {
        cfg.capacity = SENSOR_RING_BUFFER_DEFAULT_CAPACITY;
    }

    // Allocate sample buffer
    rb->buffer = (sensor_sample_t *)malloc(cfg.capacity * sizeof(sensor_sample_t));
    if (rb->buffer == NULL) {
        return SENSOR_RING_BUFFER_ERR_NO_MEM;
    }

    // Create mutex for thread safety
    rb->mutex = os_mutex_create();
    if (rb->mutex == NULL) {
        free(rb->buffer);
        rb->buffer = NULL;
        return SENSOR_RING_BUFFER_ERR_NO_MEM;
    }

    rb->capacity = cfg.capacity;
    rb->sensor_type = cfg.sensor_type;
    rb->head = 0;
    rb->tail = 0;
    rb->count = 0;
    rb->initialized = true;

    return SENSOR_RING_BUFFER_OK;
}

sensor_ring_buffer_status_t sensor_ring_buffer_deinit(sensor_ring_buffer_t *rb)
{
    if (rb == NULL) {
        return SENSOR_RING_BUFFER_ERR_INVALID_ARG;
    }

    if (!rb->initialized) {
        return SENSOR_RING_BUFFER_ERR_NOT_INIT;
    }

    // Free resources
    if (rb->mutex != NULL) {
        os_mutex_delete(rb->mutex);
        rb->mutex = NULL;
    }

    if (rb->buffer != NULL) {
        free(rb->buffer);
        rb->buffer = NULL;
    }

    rb->initialized = false;
    rb->count = 0;
    rb->head = 0;
    rb->tail = 0;

    return SENSOR_RING_BUFFER_OK;
}

bool sensor_ring_buffer_is_initialized(const sensor_ring_buffer_t *rb)
{
    if (rb == NULL) {
        return false;
    }
    return rb->initialized;
}

sensor_ring_buffer_status_t sensor_ring_buffer_push(
    sensor_ring_buffer_t *rb,
    const sensor_sample_t *sample)
{
    if (rb == NULL || sample == NULL) {
        return SENSOR_RING_BUFFER_ERR_INVALID_ARG;
    }

    if (!rb->initialized) {
        return SENSOR_RING_BUFFER_ERR_NOT_INIT;
    }

    os_mutex_take(rb->mutex, OS_WAIT_FOREVER);

    // Write sample at head position
    rb->buffer[rb->head] = *sample;

    // Advance head
    rb->head = (rb->head + 1) % rb->capacity;

    if (rb->count < rb->capacity) {
        // Buffer not yet full
        rb->count++;
    } else {
        // Buffer full - tail advances (oldest sample overwritten)
        rb->tail = (rb->tail + 1) % rb->capacity;
    }

    os_mutex_give(rb->mutex);

    return SENSOR_RING_BUFFER_OK;
}

uint32_t sensor_ring_buffer_get_count(const sensor_ring_buffer_t *rb)
{
    if (rb == NULL || !rb->initialized) {
        return 0;
    }
    return rb->count;
}

uint32_t sensor_ring_buffer_get_capacity(const sensor_ring_buffer_t *rb)
{
    if (rb == NULL || !rb->initialized) {
        return 0;
    }
    return rb->capacity;
}

sensor_ring_buffer_status_t sensor_ring_buffer_read(
    sensor_ring_buffer_t *rb,
    uint32_t start_index,
    sensor_sample_t *samples,
    uint32_t max_samples,
    uint32_t *samples_read)
{
    if (rb == NULL || samples == NULL || samples_read == NULL) {
        return SENSOR_RING_BUFFER_ERR_INVALID_ARG;
    }

    if (!rb->initialized) {
        return SENSOR_RING_BUFFER_ERR_NOT_INIT;
    }

    os_mutex_take(rb->mutex, OS_WAIT_FOREVER);

    *samples_read = 0;

    if (rb->count == 0) {
        os_mutex_give(rb->mutex);
        return SENSOR_RING_BUFFER_ERR_EMPTY;
    }

    // Validate start_index
    if (start_index >= rb->count) {
        os_mutex_give(rb->mutex);
        return SENSOR_RING_BUFFER_ERR_INVALID_ARG;
    }

    // Calculate how many samples we can read
    uint32_t available = rb->count - start_index;
    uint32_t to_read = (max_samples < available) ? max_samples : available;

    // Calculate actual buffer index for start_index
    // start_index 0 = oldest = tail
    uint32_t buf_index = (rb->tail + start_index) % rb->capacity;

    for (uint32_t i = 0; i < to_read; i++) {
        samples[i] = rb->buffer[buf_index];
        buf_index = (buf_index + 1) % rb->capacity;
    }

    *samples_read = to_read;

    os_mutex_give(rb->mutex);

    return SENSOR_RING_BUFFER_OK;
}

sensor_ring_buffer_status_t sensor_ring_buffer_peek(
    sensor_ring_buffer_t *rb,
    uint32_t index,
    sensor_sample_t *sample)
{
    if (rb == NULL || sample == NULL) {
        return SENSOR_RING_BUFFER_ERR_INVALID_ARG;
    }

    if (!rb->initialized) {
        return SENSOR_RING_BUFFER_ERR_NOT_INIT;
    }

    os_mutex_take(rb->mutex, OS_WAIT_FOREVER);

    if (rb->count == 0) {
        os_mutex_give(rb->mutex);
        return SENSOR_RING_BUFFER_ERR_EMPTY;
    }

    if (index >= rb->count) {
        os_mutex_give(rb->mutex);
        return SENSOR_RING_BUFFER_ERR_INVALID_ARG;
    }

    // Calculate actual buffer index (index 0 = oldest = tail)
    uint32_t buf_index = (rb->tail + index) % rb->capacity;
    *sample = rb->buffer[buf_index];

    os_mutex_give(rb->mutex);

    return SENSOR_RING_BUFFER_OK;
}

sensor_ring_buffer_status_t sensor_ring_buffer_clear(sensor_ring_buffer_t *rb)
{
    if (rb == NULL) {
        return SENSOR_RING_BUFFER_ERR_INVALID_ARG;
    }

    if (!rb->initialized) {
        return SENSOR_RING_BUFFER_ERR_NOT_INIT;
    }

    os_mutex_take(rb->mutex, OS_WAIT_FOREVER);

    rb->head = 0;
    rb->tail = 0;
    rb->count = 0;

    os_mutex_give(rb->mutex);

    return SENSOR_RING_BUFFER_OK;
}

sensor_type_t sensor_ring_buffer_get_sensor_type(const sensor_ring_buffer_t *rb)
{
    if (rb == NULL) {
        return 0;
    }
    return rb->sensor_type;
}
