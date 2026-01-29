#include "serv_current_monitor.h"
#include "ina226.h"
#include "hal_rtc.h"
#include "hal_delay.h"
#include "bsp.h"
#include <string.h>

// Ring buffer for samples
static current_sample_t sample_buffer[CURRENT_MONITOR_BUFFER_SIZE];
static volatile uint32_t sample_count = 0;

// Statistics
static current_monitor_stats_t stats = {0};

// Sensor reference
static ina226_sensor_t* current_sensor = &default_ina226_sensor;

// Measurement session configuration
static measurement_config_t active_config = {0};
static uint32_t measurement_start_tick = 0;
static uint32_t measurement_duration_ms = 0;
static volatile measurement_status_t measurement_status = MEASUREMENT_IDLE;

// Current state machine state (set externally)
static volatile uint8_t current_state = 0;

// Measurement session timestamp
static uint32_t session_start_sec = 0;
static uint16_t session_start_ms = 0;
static uint32_t session_start_tick = 0;

// Sample rate throttling
static uint32_t last_sample_tick = 0;
static uint32_t sample_period_ms = 0;

// Forward declarations
static void current_data_ready_callback(ina226_sensor_t* sensor, INA226_Data* data);
static void get_ina226_config_for_period(sample_period_ms_t period, ina226_config_t *config);
static void check_measurement_completion(void);

void current_monitor_init(void) {
    // Initialize INA226 driver
    ina226_init();
    
    // Clear buffer and statistics
    current_monitor_clear();
}

bool current_monitor_validate_config(const measurement_config_t *config) {
    if (config == NULL) {
        return false;
    }
    
    // Validate duration (1 second to 1 hour)
    if (config->duration_sec < 1 || config->duration_sec > 3600) {
        return false;
    }
    
    // Validate sample period
    if (config->sample_period != SAMPLE_PERIOD_1MS &&
        config->sample_period != SAMPLE_PERIOD_10MS &&
        config->sample_period != SAMPLE_PERIOD_100MS &&
        config->sample_period != SAMPLE_PERIOD_1000MS) {
        return false;
    }
    
    // Calculate expected sample count
    uint32_t expected_samples = (config->duration_sec * 1000) / config->sample_period;
    
    // Check if buffer can hold all samples
    if (expected_samples > CURRENT_MONITOR_BUFFER_SIZE) {
        return false;  // Too many samples for buffer
    }
    
    return true;
}

bool current_monitor_start_measurement(const measurement_config_t *config) {
    // Validate configuration
    if (!current_monitor_validate_config(config)) {
        return false;
    }
    
    // Don't start if already running
    if (measurement_status == MEASUREMENT_RUNNING) {
        return false;
    }
    
    // Clear previous data
    current_monitor_clear();
    
    // Store configuration
    memcpy(&active_config, config, sizeof(measurement_config_t));
    active_config.max_samples = (config->duration_sec * 1000) / config->sample_period;
    
    // Get INA226 configuration for requested sample period
    ina226_config_t ina_config;
    get_ina226_config_for_period(config->sample_period, &ina_config);
    
    // Open sensor
    hal_i2c_status_t status = ina226_open(
        current_sensor,
        BSP_Get_CurrentSensor_I2C(),
        0.1f,  // 100mΩ shunt resistor
        current_data_ready_callback,
        &ina_config
    );
    
    if (status != HAL_I2C_OK) {
        measurement_status = MEASUREMENT_ERROR;
        return false;
    }
    
    // Capture session start timestamp
    hal_rtc_time_t start_time;
    if (hal_rtc_get_time(&start_time) == HAL_RTC_OK) {
        session_start_sec = start_time.seconds;
        session_start_ms = start_time.milliseconds;
    }
    session_start_tick = hal_get_tick();
    
    // Initialize sample rate throttling
    sample_period_ms = config->sample_period;
    last_sample_tick = hal_get_tick();
    
    // Start measurement timing
    measurement_start_tick = hal_get_tick();
    measurement_duration_ms = config->duration_sec * 1000;
    measurement_status = MEASUREMENT_RUNNING;
    
    // Update stats
    stats.status = MEASUREMENT_RUNNING;
    stats.sample_period = config->sample_period;
    stats.actual_sample_rate_hz = 1000.0f / config->sample_period;
    stats.measurement_progress_percent = 0;
    
    return true;
}

void current_monitor_stop_measurement(void) {
    if (measurement_status == MEASUREMENT_RUNNING) {
        ina226_close(current_sensor);
        measurement_status = MEASUREMENT_IDLE;
        stats.status = MEASUREMENT_IDLE;
    }
}

measurement_status_t current_monitor_get_status(void) {
    // Check for completion on every status query
    if (measurement_status == MEASUREMENT_RUNNING) {
        check_measurement_completion();
    }
    return measurement_status;
}

bool current_monitor_is_complete(void) {
    return (measurement_status == MEASUREMENT_COMPLETE);
}

void current_monitor_set_state(uint8_t state) {
    current_state = state;
}

void current_monitor_process(void) {
    // Trigger INA226 to process any pending alerts
    ina226_process_alert(current_sensor);
    
    // Check measurement completion
    if (measurement_status == MEASUREMENT_RUNNING) {
        check_measurement_completion();
    }
}

uint32_t current_monitor_read_measurement(current_sample_t *samples, uint32_t max_samples) {
    if (samples == NULL || max_samples == 0) {
        return 0;
    }
    
    // Only allow reading when measurement is complete
    if (measurement_status != MEASUREMENT_COMPLETE) {
        return 0;
    }
    
    // Read all available samples (up to max_samples)
    uint32_t to_read = (sample_count < max_samples) ? sample_count : max_samples;
    
    if (to_read > 0) {
        memcpy(samples, sample_buffer, to_read * sizeof(current_sample_t));
    }
    
    return to_read;
}

void current_monitor_get_stats(current_monitor_stats_t *stats_out) {
    if (stats_out != NULL) {
        __disable_irq();
        memcpy(stats_out, &stats, sizeof(current_monitor_stats_t));
        __enable_irq();
    }
}

void current_monitor_clear(void) {
    __disable_irq();
    sample_count = 0;
    memset(&stats, 0, sizeof(current_monitor_stats_t));
    stats.buffer_full = false;
    stats.status = MEASUREMENT_IDLE;
    measurement_status = MEASUREMENT_IDLE;
    __enable_irq();
}

bool current_monitor_get_instant_reading(INA226_Data *data) {
    if (data == NULL) {
        return false;
    }
    
    hal_i2c_status_t status = ina226_read(current_sensor, data);
    return (status == HAL_I2C_OK);
}

// ========== Internal Callback ==========

static void current_data_ready_callback(ina226_sensor_t* sensor, INA226_Data* data) {
    // This callback is called from ina226_process_alert() when new data is available
    // Keep this fast - just buffer the data with timestamp and state
    
    // Don't buffer if not running or buffer full
    if (measurement_status != MEASUREMENT_RUNNING) {
        return;
    }
    
    // Throttle to desired sample rate
    uint32_t now = hal_get_tick();
    if ((now - last_sample_tick) < sample_period_ms) {
        return;  // Too soon, skip this sample
    }
    last_sample_tick = now;
    
    if (sample_count >= CURRENT_MONITOR_BUFFER_SIZE || sample_count >= active_config.max_samples) {
        stats.buffer_full = true;
        stats.buffer_overruns++;
        return;  // Buffer full, drop sample
    }
    
    // Calculate timestamp based on session start + elapsed ticks
    uint32_t elapsed_ms = hal_get_tick() - session_start_tick;
    uint32_t timestamp_sec = session_start_sec + (elapsed_ms / 1000);
    uint16_t timestamp_ms = session_start_ms + (elapsed_ms % 1000);
    
    // Handle millisecond overflow
    if (timestamp_ms >= 1000) {
        timestamp_sec += 1;
        timestamp_ms -= 1000;
    }
    
    // Store sample in buffer (sequential, not ring buffer since we read all at end)
    current_sample_t* sample = &sample_buffer[sample_count];
    sample->timestamp_sec = timestamp_sec;
    sample->timestamp_ms = timestamp_ms;
    sample->state_machine_state = current_state;
    sample->current_mA = data->current_mA;
    sample->voltage_V = data->voltage_V;
    sample->power_mW = data->power_mW;
    
    sample_count++;
    
    // Update statistics
    stats.samples_captured++;
    stats.last_read_time_sec = timestamp_sec;
    stats.last_read_time_ms = timestamp_ms;
    
    // Update progress
    if (active_config.max_samples > 0) {
        stats.measurement_progress_percent = (sample_count * 100) / active_config.max_samples;
    }
}

static void check_measurement_completion(void) {
    if (measurement_status != MEASUREMENT_RUNNING) {
        return;
    }
    
    uint32_t elapsed_ms = hal_get_tick() - measurement_start_tick;
    
    // Check if measurement duration reached OR buffer full
    if (elapsed_ms >= measurement_duration_ms || sample_count >= active_config.max_samples) {
        // Stop sensor
        ina226_close(current_sensor);
        
        // Mark as complete
        measurement_status = MEASUREMENT_COMPLETE;
        stats.status = MEASUREMENT_COMPLETE;
        stats.measurement_progress_percent = 100;
    }
}

// ========== INA226 Configuration Helper ==========

static void get_ina226_config_for_period(sample_period_ms_t period, ina226_config_t *config) {
    switch (period) {
        case SAMPLE_PERIOD_1MS:
            // 1ms period → 1000 Hz target
            // Conversion time: (332µs + 332µs) * 1 = 664µs → ~1506 Hz
            // Runs at ~1.5x target rate, throttle to 1000 Hz in software
            config->averaging = INA226_CONFIG_AVG_1;
            config->bus_conv_time = INA226_CONFIG_VBUSCT_332US;
            config->shunt_conv_time = INA226_CONFIG_VSHCT_332US;
            config->mode = INA226_CONFIG_MODE_SHUNT_BUS_CONT;
            break;
            
        case SAMPLE_PERIOD_10MS:
            // 10ms period → 100 Hz target
            // Conversion time: (588µs + 588µs) * 4 = 4.7ms → ~212 Hz
            // Runs at ~2.1x target rate with 4x averaging, throttle to 100 Hz
            config->averaging = INA226_CONFIG_AVG_4;
            config->bus_conv_time = INA226_CONFIG_VBUSCT_588US;
            config->shunt_conv_time = INA226_CONFIG_VSHCT_588US;
            config->mode = INA226_CONFIG_MODE_SHUNT_BUS_CONT;
            break;
            
        case SAMPLE_PERIOD_100MS:
            // 100ms period → 10 Hz target
            // Conversion time: (1100µs + 1100µs) * 16 = 35.2ms → ~28 Hz
            // Runs at ~2.8x target rate with 16x averaging, throttle to 10 Hz
            config->averaging = INA226_CONFIG_AVG_16;
            config->bus_conv_time = INA226_CONFIG_VBUSCT_1100US;
            config->shunt_conv_time = INA226_CONFIG_VSHCT_1100US;
            config->mode = INA226_CONFIG_MODE_SHUNT_BUS_CONT;
            break;
            
        case SAMPLE_PERIOD_1000MS:
            // 1000ms period → 1 Hz target
            // Conversion time: (4156µs + 4156µs) * 64 = 532ms → ~1.88 Hz
            // Runs at ~1.9x target rate with 64x averaging, throttle to 1 Hz
            config->averaging = INA226_CONFIG_AVG_64;
            config->bus_conv_time = INA226_CONFIG_VBUSCT_4156US;
            config->shunt_conv_time = INA226_CONFIG_VSHCT_4156US;
            config->mode = INA226_CONFIG_MODE_SHUNT_BUS_CONT;
            break;
            
        default:
            // Default to 100ms settings
            config->averaging = INA226_CONFIG_AVG_16;
            config->bus_conv_time = INA226_CONFIG_VBUSCT_1100US;
            config->shunt_conv_time = INA226_CONFIG_VSHCT_1100US;
            config->mode = INA226_CONFIG_MODE_SHUNT_BUS_CONT;
            break;
    }
}
