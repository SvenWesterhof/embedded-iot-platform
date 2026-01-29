#include "serv_temperature_sensor.h"
#include "event_bus.h"
#include "service_events.h"
#include "ath25.h"
#include "bsp.h"
#include "sensor_ring_buffer.h"
#include "os_wrapper.h"
#include "hal_rtc.h"

// ============================================================================
// Private Data
// ============================================================================

static ath25_sensor_t* temp_sensor = &default_ath25_sensor;

static uint32_t last_read_time = 0;
static const uint32_t read_interval_ms = 1000; // read every second

// Buffer state
static sensor_ring_buffer_t temp_buffer;
static bool buffer_initialized = false;
static uint32_t last_buffer_store_time = 0;
static float last_valid_temperature = 0.0f;
static bool has_valid_reading = false;
static temp_sensor_timestamp_fn timestamp_fn = NULL;

// ============================================================================
// Private Functions
// ============================================================================

static uint32_t get_timestamp(void)
{
    if (timestamp_fn != NULL) {
        return timestamp_fn();
    }
    // Use RTC timestamp if valid, otherwise fall back to OS time
    if (hal_rtc_is_time_valid()) {
        return hal_rtc_get_timestamp();
    }
    return os_get_time_ms();
}

static void store_sample_to_buffer(float temperature)
{
    if (!buffer_initialized) {
        return;
    }

    sensor_sample_t sample = {
        .sensor_type = SENSOR_TEMPERATURE,
        .timestamp = get_timestamp(),
        .value = (int32_t)(temperature * 100)  // Store as centi-degrees
    };

    sensor_ring_buffer_push(&temp_buffer, &sample);
}

// ============================================================================
// Public API - Core
// ============================================================================

void temperature_sensor_init(void)
{
    // Initialize hardware
    ath25_init();
    if(ath25_open(temp_sensor, BSP_Get_TempSensor_I2C()) != HAL_I2C_OK) {
        // Handle error
    }

    // Initialize buffer
    sensor_ring_buffer_config_t buf_config = sensor_ring_buffer_get_default_config();
    buf_config.sensor_type = SENSOR_TEMPERATURE;

    if (sensor_ring_buffer_init(&temp_buffer, &buf_config) == SENSOR_RING_BUFFER_OK) {
        buffer_initialized = true;
    }
}

void temperature_sensor_run(void)
{
    uint32_t now = hal_get_tick();

    // Read temperature sensor every second
    if ((now - last_read_time) >= read_interval_ms)
    {
        ath_data_t data;
        temperature_data_t temp_event_data;

        if(ath25_read(temp_sensor, &data) == HAL_I2C_OK) {
            // Successfully read data, publish event
            temp_event_data.temperature = data.temperature;
            temp_event_data.humidity = data.humidity;
            temp_event_data.sensor_ok = 1;

            // Store for buffering
            last_valid_temperature = data.temperature;
            has_valid_reading = true;

            // Publish event to notify subscribers
            event_bus_publish(EVENT_TEMPERATURE_UPDATED, &temp_event_data, sizeof(temperature_data_t));
        } else {
            // Handle read error - publish error event
            temp_event_data.temperature = 0.0f;
            temp_event_data.humidity = 0.0f;
            temp_event_data.sensor_ok = 0;

            event_bus_publish(EVENT_SENSOR_ERROR, &temp_event_data, sizeof(temperature_data_t));
        }
        last_read_time = now;
    }

    // Store to buffer every TEMP_SENSOR_BUFFER_INTERVAL_MS
    if (has_valid_reading && (now - last_buffer_store_time) >= TEMP_SENSOR_BUFFER_INTERVAL_MS)
    {
        store_sample_to_buffer(last_valid_temperature);
        last_buffer_store_time = now;
    }
}

// ============================================================================
// Public API - Buffer
// ============================================================================

uint32_t temperature_sensor_buffer_get_count(void)
{
    if (!buffer_initialized) {
        return 0;
    }
    return sensor_ring_buffer_get_count(&temp_buffer);
}

bool temperature_sensor_buffer_read(
    uint32_t start_index,
    sensor_sample_t *samples,
    uint32_t max_samples,
    uint32_t *samples_read)
{
    if (!buffer_initialized || samples == NULL || samples_read == NULL) {
        return false;
    }

    sensor_ring_buffer_status_t status = sensor_ring_buffer_read(
        &temp_buffer, start_index, samples, max_samples, samples_read);

    return (status == SENSOR_RING_BUFFER_OK);
}

void temperature_sensor_buffer_clear(void)
{
    if (!buffer_initialized) {
        return;
    }
    sensor_ring_buffer_clear(&temp_buffer);
}

void temperature_sensor_set_timestamp_fn(temp_sensor_timestamp_fn get_timestamp)
{
    timestamp_fn = get_timestamp;
}
