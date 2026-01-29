#ifndef INA226_H
#define INA226_H

#include "hal_i2c.h"
#include "hal_gpio.h"
#include "hal_delay.h"
#include "pinout.h"
#include <stdbool.h>
#include <stdint.h>

// INA226 I2C Address
#define INA226_I2C_ADDRESS          (0x40 << 1)

// INA226 configuration Register Addresses
#define INA226_REG_CONFIG           0x00
#define INA226_REG_SHUNT_VOLTAGE    0x01
#define INA226_REG_BUS_VOLTAGE      0x02
#define INA226_REG_POWER            0x03
#define INA226_REG_CURRENT          0x04
#define INA226_REG_CALIBRATION      0x05
#define INA226_REG_MASK_ENABLE      0x06
#define INA226_REG_ALERT_LIMIT      0x07
#define INA226_REG_MANUFACTURER_ID  0xFE
#define INA226_REG_DIE_ID           0xFF

// Configuration Register Bits
#define INA226_CONFIG_RESET         0x8000
#define INA226_CONFIG_MODE_MASK     0x0007
#define INA226_CONFIG_MODE_POWERDOWN    0x0000
#define INA226_CONFIG_MODE_SHUNT_TRIG   0x0001
#define INA226_CONFIG_MODE_BUS_TRIG     0x0002
#define INA226_CONFIG_MODE_SHUNT_BUS_TRIG 0x0003
#define INA226_CONFIG_MODE_POWERDOWN2   0x0004
#define INA226_CONFIG_MODE_SHUNT_CONT   0x0005
#define INA226_CONFIG_MODE_BUS_CONT     0x0006
#define INA226_CONFIG_MODE_SHUNT_BUS_CONT 0x0007

// Averaging modes
#define INA226_CONFIG_AVG_1         0x0000
#define INA226_CONFIG_AVG_4         0x0200
#define INA226_CONFIG_AVG_16        0x0400
#define INA226_CONFIG_AVG_64        0x0600
#define INA226_CONFIG_AVG_128       0x0800
#define INA226_CONFIG_AVG_256       0x0A00
#define INA226_CONFIG_AVG_512       0x0C00
#define INA226_CONFIG_AVG_1024      0x0E00

// Conversion time
#define INA226_CONFIG_VBUSCT_140US  0x0000
#define INA226_CONFIG_VBUSCT_204US  0x0040
#define INA226_CONFIG_VBUSCT_332US  0x0080
#define INA226_CONFIG_VBUSCT_588US  0x00C0
#define INA226_CONFIG_VBUSCT_1100US 0x0100
#define INA226_CONFIG_VBUSCT_2116US 0x0140
#define INA226_CONFIG_VBUSCT_4156US 0x0180
#define INA226_CONFIG_VBUSCT_8244US 0x01C0

#define INA226_CONFIG_VSHCT_140US   0x0000
#define INA226_CONFIG_VSHCT_204US   0x0008
#define INA226_CONFIG_VSHCT_332US   0x0010
#define INA226_CONFIG_VSHCT_588US   0x0018
#define INA226_CONFIG_VSHCT_1100US  0x0020
#define INA226_CONFIG_VSHCT_2116US  0x0028
#define INA226_CONFIG_VSHCT_4156US  0x0030
#define INA226_CONFIG_VSHCT_8244US  0x0038

// Default I2C timeout
#define INA226_I2C_TIMEOUT_MS       100

// Measurement configuration structure
typedef struct {
    uint16_t averaging;         // Use INA226_CONFIG_AVG_* defines
    uint16_t bus_conv_time;     // Use INA226_CONFIG_VBUSCT_* defines
    uint16_t shunt_conv_time;   // Use INA226_CONFIG_VSHCT_* defines
    uint16_t mode;              // Use INA226_CONFIG_MODE_* defines
} ina226_config_t;

// Data structure for measurements
typedef struct {
    float current_mA;
    float voltage_V;
    float power_mW;
} INA226_Data;

// Forward declaration
typedef struct ina226_sensor ina226_sensor_t;

// Data ready callback function type
typedef void (*ina226_data_callback_t)(ina226_sensor_t* sensor, INA226_Data* data);

// INA226 sensor configuration
typedef struct ina226_sensor {
    bool initialized;
    bool active;
    hal_i2c_handle_t hi2c;
    uint16_t i2c_address;
    hal_gpio_port_t alert_port;
    hal_gpio_pin_t alert_pin;
    float shunt_resistor_ohms;  // Shunt resistor value in ohms
    float current_lsb;          // Current LSB in A
    uint16_t calibration_value; // Calibration register value
    volatile bool alert_flag;   // Set by ISR, cleared by application
    ina226_data_callback_t data_callback; // Direct callback for high-frequency data
} ina226_sensor_t;

extern ina226_sensor_t default_ina226_sensor;

// Driver functions

/**
 * @brief Initialize INA226 driver
 * Configures I2C, shunt resistor scaling, mode defaults, prepares ALERT pin interrupt
 * Sensor is not yet active
 */
void ina226_init(void);

/**
 * @brief Open connection and start sensor
 * Starts sensor in continuous measurement mode, configures averaging/conversion time,
 * activates alerts. Ready to read measurements.
 * 
 * @param sensor Pointer to INA226 sensor structure
 * @param hi2c I2C handle
 * @param shunt_resistor_ohms Shunt resistor value in ohms (e.g., 0.1 for 100mΩ)
 * @param data_callback Callback for data-ready events (called from process_alert)
 * @param config Optional configuration (NULL for default: 16x avg, 1.1ms conversion)
 * @return hal_i2c_status_t Status of operation
 */
hal_i2c_status_t ina226_open(ina226_sensor_t *sensor, hal_i2c_handle_t hi2c, 
                              float shunt_resistor_ohms, ina226_data_callback_t data_callback,
                              const ina226_config_t *config);

/**
 * @brief Configure measurement parameters
 * Changes conversion times, averaging, and measurement mode.
 * Can be called while sensor is active to change settings on-the-fly.
 * 
 * @param sensor Pointer to INA226 sensor structure
 * @param config Pointer to configuration structure with measurement parameters
 * @return hal_i2c_status_t Status of operation
 */
hal_i2c_status_t ina226_configure(ina226_sensor_t *sensor, const ina226_config_t *config);

/**
 * @brief Read current measurements from sensor
 * Reads actual current, bus voltage and calculates power
 * 
 * @param sensor Pointer to INA226 sensor structure
 * @param data Pointer to data structure to store measurements
 * @return hal_i2c_status_t Status of operation
 */
hal_i2c_status_t ina226_read(ina226_sensor_t *sensor, INA226_Data *data);

/**
 * @brief Close sensor and stop measurements
 * Stops sensor / puts in power-down mode, disables ADC conversions,
 * disables ALERT interrupt. Sensor is "off" but can be opened again later.
 * 
 * @param sensor Pointer to INA226 sensor structure
 * @return hal_i2c_status_t Status of operation
 */
hal_i2c_status_t ina226_close(ina226_sensor_t *sensor);

/**
 * @brief Deinitialize sensor completely
 * Puts sensor in safe power-down, disables I2C peripheral clocks,
 * cleanup GPIO pins. MCU ready for deep sleep.
 * 
 * @param sensor Pointer to INA226 sensor structure
 */
void ina226_deinit(ina226_sensor_t *sensor);

/**
 * @brief ALERT pin interrupt callback
 * Should be called from GPIO EXTI interrupt handler.
 * Sets alert flag for processing in main context.
 * 
 * @param sensor Pointer to INA226 sensor structure
 */
void ina226_alert_callback(ina226_sensor_t *sensor);

/**
 * @brief Process pending ALERT and call data callback
 * Should be called periodically (e.g., from timer interrupt or high-priority task).
 * Reads sensor data and calls data_callback if configured.
 * 
 * @param sensor Pointer to INA226 sensor structure
 */
void ina226_process_alert(ina226_sensor_t *sensor);

#endif // INA226_H
