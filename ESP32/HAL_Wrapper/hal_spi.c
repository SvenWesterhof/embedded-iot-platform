#include "hal_spi.h"
#include "../Drivers_BSP/BSP/pinout.h"
#include <driver/spi_master.h>
#include <esp_log.h>

static const char *TAG = "HAL_SPI";

#define SPI_HOST_ID         SPI2_HOST
#define SPI_CLOCK_SPEED_HZ  10000000  // 10 MHz

static spi_device_handle_t spi_handle = NULL;

bool hal_spi_init(void)
{
    ESP_LOGI(TAG, "Initializing SPI HAL");
    
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = SPI_MOSI_PIN,
        .miso_io_num = SPI_MISO_PIN,
        .sclk_io_num = SPI_SCK_PIN,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096
    };
    
    esp_err_t err = spi_bus_initialize(SPI_HOST_ID, &bus_cfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(err));
        return false;
    }
    
    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = SPI_CLOCK_SPEED_HZ,
        .mode = 0,
        .spics_io_num = SPI_CS_PIN,
        .queue_size = 7,
        .flags = 0,
        .pre_cb = NULL
    };
    
    err = spi_bus_add_device(SPI_HOST_ID, &dev_cfg, &spi_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add SPI device: %s", esp_err_to_name(err));
        return false;
    }
    
    ESP_LOGI(TAG, "SPI HAL initialized");
    return true;
}

bool hal_spi_transmit(const uint8_t *data, size_t len)
{
    if (spi_handle == NULL) {
        ESP_LOGE(TAG, "SPI not initialized");
        return false;
    }
    
    spi_transaction_t trans = {
        .length = len * 8,
        .tx_buffer = data,
        .rx_buffer = NULL
    };
    
    esp_err_t err = spi_device_transmit(spi_handle, &trans);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPI transmit failed: %s", esp_err_to_name(err));
        return false;
    }
    
    return true;
}

bool hal_spi_receive(uint8_t *data, size_t len)
{
    if (spi_handle == NULL) {
        ESP_LOGE(TAG, "SPI not initialized");
        return false;
    }
    
    spi_transaction_t trans = {
        .length = len * 8,
        .rxlength = len * 8,
        .tx_buffer = NULL,
        .rx_buffer = data
    };
    
    esp_err_t err = spi_device_transmit(spi_handle, &trans);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPI receive failed: %s", esp_err_to_name(err));
        return false;
    }
    
    return true;
}

bool hal_spi_transfer(const uint8_t *tx_data, uint8_t *rx_data, size_t len)
{
    if (spi_handle == NULL) {
        ESP_LOGE(TAG, "SPI not initialized");
        return false;
    }
    
    spi_transaction_t trans = {
        .length = len * 8,
        .rxlength = len * 8,
        .tx_buffer = tx_data,
        .rx_buffer = rx_data
    };
    
    esp_err_t err = spi_device_transmit(spi_handle, &trans);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPI transfer failed: %s", esp_err_to_name(err));
        return false;
    }
    
    return true;
}
