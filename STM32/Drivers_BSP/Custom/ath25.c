#include "ath25.h"
#include "pinout.h"

ath25_sensor_t default_ath25_sensor = {
    .initialized = false,
    .hi2c = NULL,
    .i2c_address = 0x38 << 1, // Example I2C address
    .power_port = (hal_gpio_port_t)TEMP_SENSOR_ON_OFF_PORT,
    .power_pin = TEMP_SENSOR_ON_OFF_PIN,
};

void ath25_init() {
    // Initialize I2C, GPIOs, etc. for the temperature sensor
}

static uint8_t ath25_crc8(uint8_t *data, uint8_t len)
{
    uint8_t crc = 0xFF;

    for (uint8_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            if (crc & 0x80)
                crc = (crc << 1) ^ 0x31;  // polynomial: x8 + x5 + x4 + 1 (0x31)
            else
                crc <<= 1;
        }
    }
    return crc;
}

hal_i2c_status_t ath25_open(ath25_sensor_t *sensor, hal_i2c_handle_t hi2c) {
    uint8_t cmd = 0x71; 
    uint8_t status; 

    sensor->hi2c = hi2c;
    sensor->initialized = false; 
    
    // Power on the sensor
    hal_gpio_write_pin(sensor->power_port, sensor->power_pin, HAL_GPIO_PIN_SET);
    hal_delay_ms(100); // wait for sensor to power up

    // Check sensor status
    if (hal_i2c_master_transmit(sensor->hi2c, sensor->i2c_address, &cmd, 1, MAX_ATH25_I2C_TRANSFER_TIME) != HAL_I2C_OK) {
        return HAL_I2C_ERROR;
    }
    if (hal_i2c_master_receive(sensor->hi2c, sensor->i2c_address, &status, 1, MAX_ATH25_I2C_TRANSFER_TIME) != HAL_I2C_OK) {
        return HAL_I2C_ERROR;
    }

        // Check calibration bits must be 0x18
    if ((status & 0x18) != 0x18) {
        // Normally you'd write registers 0x1B, 0x1C, 0x1E here
        // Manufacturer: "see sample program". Many modules ship pre-calibrated.
        return HAL_I2C_ERROR;
    }
    sensor->initialized = true;
    return HAL_I2C_OK;
}

hal_i2c_status_t ath25_read(ath25_sensor_t *sensor, ath_data_t *data) {
    uint8_t cmd[3] = {0xAC, 0x33, 0x00};
    uint8_t recv[7];
    // Read temperature value from the sensor
    if (sensor->initialized == false) {
        return HAL_I2C_ERROR;
    }

    if (hal_i2c_master_transmit(sensor->hi2c, sensor->i2c_address, cmd, 3, MAX_ATH25_I2C_TRANSFER_TIME) != HAL_I2C_OK) {
        return HAL_I2C_ERROR;
    }

    hal_delay_ms(80); // wait for measurement to complete

    if (hal_i2c_master_receive(sensor->hi2c, sensor->i2c_address, recv, 7, MAX_ATH25_I2C_TRANSFER_TIME) != HAL_I2C_OK) {
        return HAL_I2C_ERROR;
    }

        // Check if busy bit clear
    if (recv[0] & 0x80)
        return HAL_I2C_BUSY;

    // Verify CRC
    if (ath25_crc8(recv, 6) != recv[6]) {
        return HAL_I2C_ERROR;
    }

    //combine bytes to get raw temperature and humidity
    uint32_t raw_temp = ((uint32_t)(recv[3] & 0x0F) << 16) | ((uint32_t)recv[4] << 8) | (uint32_t)recv[5];
    uint32_t raw_hum = ((uint32_t)recv[1] << 12) | ((uint32_t)recv[2] << 4) | ((uint32_t)(recv[3] >> 4));

    // Convert raw values to physical values
    data->temperature = ((float)raw_temp / 1048576.0f) * 200.0f - 50.0f; // -50 to 150 C
    data->humidity = ((float)raw_hum / 1048576.0f) * 100.0f; // 0 to 100 %

    return HAL_I2C_OK;
}

hal_i2c_status_t ath25_close(ath25_sensor_t *sensor) {
    if (sensor->initialized == false) {
        return HAL_I2C_ERROR;
    }
    
    // Power off the sensor
    hal_gpio_write_pin(sensor->power_port, sensor->power_pin, HAL_GPIO_PIN_RESET);
    sensor->initialized = false;
    
    return HAL_I2C_OK;
}

void ath25_deinit(ath25_sensor_t *sensor) {
    // Fully deinitialize the sensor
    if (sensor->initialized) {
        ath25_close(sensor);
    }
    
    // Clear the I2C handle reference
    sensor->hi2c = NULL;
}
