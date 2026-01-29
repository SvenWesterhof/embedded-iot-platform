#ifndef ATH25_H
#define ATH25_H

#include "hal_i2c.h"
#include "hal_gpio.h"
#include "hal_delay.h"
#include "pinout.h"
#include <stdbool.h>

// sensor is -40 to 80 degrees with an resolution of 0.01 degrees conversion -> (St / 2^20) * 200 - 50

typedef struct {
    bool initialized;
    hal_i2c_handle_t hi2c;        // I2C handle
    uint8_t i2c_address;              // I2C address of the temperature sensor
    hal_gpio_port_t power_port;       // power control pin port
    hal_gpio_pin_t power_pin;         // power control pin number
    uint8_t resolution;               // sensor resolution (bits)
} ath25_sensor_t;

extern ath25_sensor_t default_ath25_sensor;

typedef struct {
    float temperature;
    float humidity;
}ath_data_t;

#define MAX_ATH25_I2C_TRANSFER_TIME 100

// Initialize the temperature sensor (i2nc setup, GPIOs, etc.)
void ath25_init();

// Open a connection  and enable the temperature sensor
hal_i2c_status_t ath25_open(ath25_sensor_t *sensor, hal_i2c_handle_t hi2c);

// Read the current temperature value from the sensor
hal_i2c_status_t ath25_read(ath25_sensor_t *sensor, ath_data_t *data);

// Close the connection and power down the temperature sensor
hal_i2c_status_t ath25_close(ath25_sensor_t *sensor);

// Deinitialize and fully power down the sensor
void ath25_deinit(ath25_sensor_t *sensor);

#endif // ATH25_H
