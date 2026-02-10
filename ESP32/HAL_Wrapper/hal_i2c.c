#include "hal_i2c.h"
#include "../Drivers_BSP/BSP/pinout.h"
#include <driver/i2c_master.h>
#include "portable_log.h"
#include "os_wrapper.h"

static const char *TAG = "HAL_I2C";

#define I2C_TIMEOUT_MS      1000

static i2c_master_bus_handle_t bus_handle = NULL;

bool hal_i2c_init(void)
{
    LOG_I(TAG, "Initializing I2C HAL");
    
    i2c_master_bus_config_t bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = I2C_SCL_PIN,
        .sda_io_num = I2C_SDA_PIN,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    
    esp_err_t err = i2c_new_master_bus(&bus_config, &bus_handle);
    if (err != ESP_OK) {
        LOG_E(TAG, "Failed to create I2C bus: %s", esp_err_to_name(err));
        return false;
    }
    
    LOG_I(TAG, "I2C HAL initialized");
    return true;
}

bool hal_i2c_write(uint8_t addr, const uint8_t *data, size_t len)
{
    if (bus_handle == NULL) {
        LOG_E(TAG, "I2C not initialized");
        return false;
    }
    
    i2c_master_dev_handle_t dev_handle;
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = I2C_FREQ_HZ,
    };
    
    esp_err_t err = i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle);
    if (err != ESP_OK) {
        LOG_E(TAG, "Failed to add device: %s", esp_err_to_name(err));
        return false;
    }
    
    err = i2c_master_transmit(dev_handle, data, len, os_ms_to_ticks(I2C_TIMEOUT_MS));
    i2c_master_bus_rm_device(dev_handle);
    
    if (err != ESP_OK) {
        LOG_E(TAG, "I2C write failed: %s", esp_err_to_name(err));
        return false;
    }
    
    return true;
}

bool hal_i2c_read(uint8_t addr, uint8_t *data, size_t len)
{
    if (bus_handle == NULL) {
        LOG_E(TAG, "I2C not initialized");
        return false;
    }
    
    if (len == 0) {
        return true;
    }
    
    i2c_master_dev_handle_t dev_handle;
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = I2C_FREQ_HZ,
    };
    
    esp_err_t err = i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle);
    if (err != ESP_OK) {
        LOG_E(TAG, "Failed to add device: %s", esp_err_to_name(err));
        return false;
    }
    
    err = i2c_master_receive(dev_handle, data, len, os_ms_to_ticks(I2C_TIMEOUT_MS));
    i2c_master_bus_rm_device(dev_handle);
    
    if (err != ESP_OK) {
        LOG_E(TAG, "I2C read failed: %s", esp_err_to_name(err));
        return false;
    }
    
    return true;
}

bool hal_i2c_write_read(uint8_t addr, const uint8_t *write_data, size_t write_len, 
                        uint8_t *read_data, size_t read_len)
{
    if (bus_handle == NULL) {
        LOG_E(TAG, "I2C not initialized");
        return false;
    }
    
    i2c_master_dev_handle_t dev_handle;
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = I2C_FREQ_HZ,
    };
    
    esp_err_t err = i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle);
    if (err != ESP_OK) {
        LOG_E(TAG, "Failed to add device: %s", esp_err_to_name(err));
        return false;
    }
    
    err = i2c_master_transmit_receive(dev_handle, write_data, write_len, 
                                      read_data, read_len, pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    i2c_master_bus_rm_device(dev_handle);
    
    if (err != ESP_OK) {
        LOG_E(TAG, "I2C write-read failed: %s", esp_err_to_name(err));
        return false;
    }
    
    return true;
}
