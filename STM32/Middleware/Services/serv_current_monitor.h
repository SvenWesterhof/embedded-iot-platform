#ifndef SERV_CURRENT_MONITOR_H
#define SERV_CURRENT_MONITOR_H

#include <stdint.h>
#include <stdbool.h>
#include "ina226.h"

// Configuration
#define CURRENT_MONITOR_BUFFER_SIZE     4096    // Ring buffer size for samples

// Sample period options (in milliseconds)
typedef enum {
    SAMPLE_PERIOD_1MS = 1,       // 1ms = 1000 Hz (ultra precise, max ~3 seconds in buffer)
    SAMPLE_PERIOD_10MS = 10,     // 10ms = 100 Hz (precise, max ~40 seconds in buffer)
    SAMPLE_PERIOD_100MS = 100,   // 100ms = 10 Hz (normal, max ~6.8 minutes in buffer)
    SAMPLE_PERIOD_1000MS = 1000  // 1000ms = 1 Hz (low power, max ~68 minutes in buffer)
} sample_period_ms_t;

// Measurement status
typedef enum {
    MEASUREMENT_IDLE,            // No measurement active
    MEASUREMENT_RUNNING,         // Measurement in progress
    MEASUREMENT_COMPLETE,        // Measurement finished, data ready
    MEASUREMENT_ERROR            // Error occurred during measurement
} measurement_status_t;

// Measurement configuration
typedef struct {
    uint32_t duration_sec;       // Total measurement duration in seconds (1-3600)
    sample_period_ms_t sample_period; // Sample period (1ms, 10ms, 100ms, 1000ms)
    uint32_t max_samples;        // Calculated: duration_sec * 1000 / sample_period
} measurement_config_t;

// Buffered sample with timestamp and state
typedef struct {
    uint32_t timestamp_sec;      // Unix timestamp (seconds)
    uint16_t timestamp_ms;       // Milliseconds (0-999)
    uint8_t state_machine_state; // Main state machine state at time of sample
    float current_mA;
    float voltage_V;
    float power_mW;
} current_sample_t;

// Service statistics
typedef struct {
    uint32_t samples_captured;
    uint32_t buffer_overruns;
    uint32_t last_read_time_sec;
    uint16_t last_read_time_ms;
    bool buffer_full;
    sample_period_ms_t sample_period;
    float actual_sample_rate_hz;    // Actual achieved sample rate
    measurement_status_t status;
    uint32_t measurement_progress_percent; // 0-100%
} current_monitor_stats_t;

/**
 * @brief Initialize current monitoring service
 * Sets up INA226 driver and prepares ring buffer
 */
void current_monitor_init(void);

/**
 * @brief Start a new measurement session
 * Starts measurement with specified duration and sample period
 * Automatically stops when duration is reached
 * 
 * @param config Measurement configuration (duration and sample period)
 * @return true if started successfully
 */
bool current_monitor_start_measurement(const measurement_config_t *config);

/**
 * @brief Stop current measurement
 * Stops measurement before completion
 */
void current_monitor_stop_measurement(void);

/**
 * @brief Get current measurement status
 * 
 * @return Current measurement status (IDLE, RUNNING, COMPLETE, ERROR)
 */
measurement_status_t current_monitor_get_status(void);

/**
 * @brief Check if measurement is complete and data is ready
 * 
 * @return true if measurement is complete and data can be read
 */
bool current_monitor_is_complete(void);

/**
 * @brief Set current state machine state
 * Should be called whenever state machine changes state
 * This state will be attached to all subsequent samples
 * 
 * @param state Current state machine state (0-255)
 */
void current_monitor_set_state(uint8_t state);

/**
 * @brief Process pending samples (should be called from main loop)
 * Triggers INA226 to check for new data and checks measurement completion
 * Should be called frequently (e.g., every 1ms from main loop or timer)
 */
void current_monitor_process(void);

/**
 * @brief Read all captured samples from completed measurement
 * Only works when measurement status is COMPLETE
 * 
 * @param samples Pointer to array to store samples
 * @param max_samples Maximum number of samples to read
 * @return Number of samples actually read
 */
uint32_t current_monitor_read_measurement(current_sample_t *samples, uint32_t max_samples);

/**
 * @brief Get service statistics
 * 
 * @param stats Pointer to statistics structure to fill
 */
void current_monitor_get_stats(current_monitor_stats_t *stats);

/**
 * @brief Clear all buffered samples and reset to IDLE
 */
void current_monitor_clear(void);

/**
 * @brief Get current instantaneous reading (bypasses buffer)
 * 
 * @param data Pointer to INA226 data structure to fill
 * @return true if read successfully
 */
bool current_monitor_get_instant_reading(INA226_Data *data);

/**
 * @brief Validate measurement configuration
 * Checks if requested duration and sample period are feasible
 * 
 * @param config Configuration to validate
 * @return true if configuration is valid
 */
bool current_monitor_validate_config(const measurement_config_t *config);

#endif // SERV_CURRENT_MONITOR_H
