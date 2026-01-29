#include "ina226.h"
#include "pinout.h"
#include <math.h>

ina226_sensor_t default_ina226_sensor = {
    .initialized = false,
    .active = false,
    .hi2c = NULL,
    .i2c_address = INA226_I2C_ADDRESS,
    .alert_port = (hal_gpio_port_t)INA226_ALERT_PORT,
    .alert_pin = INA226_ALERT_PIN,
    .shunt_resistor_ohms = 0.1f, // Default 100mΩ
    .current_lsb = 0.0f,
    .calibration_value = 0,
    .alert_flag = false,
    .data_callback = NULL
};

// Helper function to write 16-bit register
static hal_i2c_status_t ina226_write_register(ina226_sensor_t *sensor, uint8_t reg, uint16_t value) {
    uint8_t data[2];
    data[0] = (value >> 8) & 0xFF;  // MSB first
    data[1] = value & 0xFF;         // LSB second
    
    return hal_i2c_mem_write(sensor->hi2c, sensor->i2c_address, reg, data, 2, INA226_I2C_TIMEOUT_MS);
}

// Helper function to read 16-bit register
static hal_i2c_status_t ina226_read_register(ina226_sensor_t *sensor, uint8_t reg, uint16_t *value) {
    uint8_t data[2];
    hal_i2c_status_t status;
    
    status = hal_i2c_mem_read(sensor->hi2c, sensor->i2c_address, reg, data, 2, INA226_I2C_TIMEOUT_MS);
    if (status == HAL_I2C_OK) {
        *value = ((uint16_t)data[0] << 8) | data[1];
    }
    
    return status;
}

void ina226_init(void) {
    // Basic driver initialization
    // GPIO and I2C peripheral configuration would be done by HAL layer
    default_ina226_sensor.initialized = true;
    default_ina226_sensor.active = false;
}

static hal_i2c_status_t ina226_read_data(ina226_sensor_t *sensor, INA226_Data *data) {
    uint16_t raw_current, raw_voltage, raw_power;
    hal_i2c_status_t status;
    
    if (!sensor->active) {
        return HAL_I2C_ERROR;
    }
    
    // Read bus voltage (register 0x02)
    // LSB = 1.25 mV
    status = ina226_read_register(sensor, INA226_REG_BUS_VOLTAGE, &raw_voltage);
    if (status != HAL_I2C_OK) {
        return status;
    }
    data->voltage_V = (float)raw_voltage * 0.00125f; // Convert to volts
    
    // Read current (register 0x04)
    // Value is in Current_LSB units (can be negative for bidirectional measurements)
    status = ina226_read_register(sensor, INA226_REG_CURRENT, &raw_current);
    if (status != HAL_I2C_OK) {
        return status;
    }
    
    // Handle signed value
    int16_t signed_current = (int16_t)raw_current;
    data->current_mA = (float)signed_current * sensor->current_lsb * 1000.0f; // Convert to mA
    
    // Read power (register 0x03)
    // LSB = 25 * Current_LSB
    status = ina226_read_register(sensor, INA226_REG_POWER, &raw_power);
    if (status != HAL_I2C_OK) {
        return status;
    }
    data->power_mW = (float)raw_power * 25.0f * sensor->current_lsb * 1000.0f; // Convert to mW
    
    return HAL_I2C_OK;
}

hal_i2c_status_t ina226_open(ina226_sensor_t *sensor, hal_i2c_handle_t hi2c, 
                              float shunt_resistor_ohms, ina226_data_callback_t data_callback,
                              const ina226_config_t *config) {
    uint16_t device_id;
    hal_i2c_status_t status;
    
    if (sensor->active) {
        return HAL_I2C_ERROR; // Already open
    }
    
    sensor->hi2c = hi2c;
    sensor->shunt_resistor_ohms = shunt_resistor_ohms;
    sensor->data_callback = data_callback;
    
    // Verify device ID (should be 0x5449 for INA226)
    status = ina226_read_register(sensor, INA226_REG_MANUFACTURER_ID, &device_id);
    if (status != HAL_I2C_OK) {
        return status;
    }
    
    if (device_id != 0x5449) {
        return HAL_I2C_ERROR; // Wrong device ID
    }
    
    // Reset the device
    status = ina226_write_register(sensor, INA226_REG_CONFIG, INA226_CONFIG_RESET);
    if (status != HAL_I2C_OK) {
        return status;
    }
    
    // Wait for reset to complete
    hal_delay_ms(10);
    
    // Calculate calibration value
    // Current_LSB = Maximum Expected Current / 32768
    // For example: If max current is 3.2A, Current_LSB = 3.2 / 32768 = 0.0001 A = 0.1 mA
    float max_expected_current = 3.2f; // 3.2A max
    sensor->current_lsb = max_expected_current / 32768.0f;
    
    // Calibration = 0.00512 / (Current_LSB * R_shunt)
    float cal = 0.00512f / (sensor->current_lsb * sensor->shunt_resistor_ohms);
    sensor->calibration_value = (uint16_t)cal;
    
    status = ina226_write_register(sensor, INA226_REG_CALIBRATION, sensor->calibration_value);
    if (status != HAL_I2C_OK) {
        return status;
    }
    
    // Configure sensor with provided config or use defaults
    uint16_t config_value;
    if (config != NULL) {
        // Use provided configuration
        config_value = config->averaging | 
                      config->bus_conv_time | 
                      config->shunt_conv_time | 
                      config->mode;
    } else {
        // Use default configuration:
        // - Averaging: 16 samples
        // - Bus voltage conversion time: 1.1ms
        // - Shunt voltage conversion time: 1.1ms
        // - Mode: Continuous shunt and bus voltage
        config_value = INA226_CONFIG_AVG_16 |
                      INA226_CONFIG_VBUSCT_1100US |
                      INA226_CONFIG_VSHCT_1100US |
                      INA226_CONFIG_MODE_SHUNT_BUS_CONT;
    }
    
    status = ina226_write_register(sensor, INA226_REG_CONFIG, config_value);
    if (status != HAL_I2C_OK) {
        return status;
    }
    
    sensor->active = true;
    return HAL_I2C_OK;
}

hal_i2c_status_t ina226_configure(ina226_sensor_t *sensor, const ina226_config_t *config) {
    if (!sensor->active) {
        return HAL_I2C_ERROR;
    }
    
    // Build configuration register value
    uint16_t config_value = config->averaging | 
                           config->bus_conv_time | 
                           config->shunt_conv_time | 
                           config->mode;
    
    // Write configuration to sensor
    return ina226_write_register(sensor, INA226_REG_CONFIG, config_value);
}

hal_i2c_status_t ina226_read(ina226_sensor_t *sensor, INA226_Data *data) {
    uint16_t raw_current, raw_voltage, raw_power;
    hal_i2c_status_t status;
    
    if (!sensor->active) {
        return HAL_I2C_ERROR;
    }
    
    // Read bus voltage (register 0x02)
    // LSB = 1.25 mV
    status = ina226_read_register(sensor, INA226_REG_BUS_VOLTAGE, &raw_voltage);
    if (status != HAL_I2C_OK) {
        return status;
    }
    data->voltage_V = (float)raw_voltage * 0.00125f; // Convert to volts
    
    // Read current (register 0x04)
    // Value is in Current_LSB units (can be negative for bidirectional measurements)
    status = ina226_read_register(sensor, INA226_REG_CURRENT, &raw_current);
    if (status != HAL_I2C_OK) {
        return status;
    }
    
    // Handle signed value
    int16_t signed_current = (int16_t)raw_current;
    data->current_mA = (float)signed_current * sensor->current_lsb * 1000.0f; // Convert to mA
    
    // Read power (register 0x03)
    // LSB = 25 * Current_LSB
    status = ina226_read_register(sensor, INA226_REG_POWER, &raw_power);
    if (status != HAL_I2C_OK) {
        return status;
    }
    data->power_mW = (float)raw_power * 25.0f * sensor->current_lsb * 1000.0f; // Convert to mW
    
    return HAL_I2C_OK;
}

hal_i2c_status_t ina226_close(ina226_sensor_t *sensor) {
    if (!sensor->active) {
        return HAL_I2C_ERROR;
    }
    
    // Put sensor in power-down mode
    hal_i2c_status_t status = ina226_write_register(sensor, INA226_REG_CONFIG, INA226_CONFIG_MODE_POWERDOWN);
    if (status != HAL_I2C_OK) {
        return status;
    }
    
    sensor->active = false;
    return HAL_I2C_OK;
}

void ina226_deinit(ina226_sensor_t *sensor) {
    // Close sensor if still active
    if (sensor->active) {
        ina226_close(sensor);
    }
    
    // Clear I2C handle reference
    sensor->hi2c = NULL;
    sensor->initialized = false;
}

void ina226_alert_callback(ina226_sensor_t *sensor) {
    // This function is called from ISR context
    // Keep it minimal - just set the flag
    sensor->alert_flag = true;
}

void ina226_process_alert(ina226_sensor_t *sensor) {
    // Check if alert flag is set
    if (sensor->alert_flag) {
        // Clear the flag
        sensor->alert_flag = false;
        
        // Read sensor data
        INA226_Data data;
        hal_i2c_status_t status = ina226_read_data(sensor, &data);
        
        if (status == HAL_I2C_OK) {
            // Direct callback for high-frequency data acquisition
            if (sensor->data_callback != NULL) {
                sensor->data_callback(sensor, &data);
            }
        }
    }
}
