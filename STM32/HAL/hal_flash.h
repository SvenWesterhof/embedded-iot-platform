#ifndef HAL_FLASH_H
#define HAL_FLASH_H

/**
 * @file hal_flash.h
 * @brief Flash memory abstraction layer for dual-bank OTA
 *
 * Provides flash erase, write, read operations for STM32F767 dual-bank flash.
 * Bank 1 (sectors 1-11) = application, Bank 2 = OTA staging.
 * Bootloader at sector 0 handles copying from Bank 2 → Bank 1 on update.
 */

#include <stdint.h>
#include <stdbool.h>

// Flash bank identifiers (match STM32 HAL defines)
#define HAL_FLASH_BANK_1    1
#define HAL_FLASH_BANK_2    2

// Flash base addresses
#define HAL_FLASH_BANK1_BASE  0x08000000U
#define HAL_FLASH_BANK2_BASE  0x08100000U
#define HAL_FLASH_BANK_SIZE   0x00100000U  // 1MB per bank

// Status codes
typedef enum {
    HAL_FL_OK = 0,
    HAL_FL_ERR_ERASE,
    HAL_FL_ERR_WRITE,
    HAL_FL_ERR_LOCK,
    HAL_FL_ERR_INVALID_ARG,
    HAL_FL_ERR_ALIGNMENT,
} hal_flash_status_t;

/**
 * @brief Initialize flash HAL
 *
 * Ensures dual-bank mode is enabled and BOOT_ADD0 points to Bank 1.
 *
 * @return HAL_FL_OK on success
 */
hal_flash_status_t hal_flash_init(void);

/**
 * @brief Erase all sectors of a flash bank
 * @param bank HAL_FLASH_BANK_1 or HAL_FLASH_BANK_2
 * @return HAL_FL_OK on success
 */
hal_flash_status_t hal_flash_erase_bank(uint8_t bank);

/**
 * @brief Write data to flash memory
 * @param address Target flash address (must be word-aligned)
 * @param data Data to write
 * @param length Number of bytes to write
 * @return HAL_FL_OK on success
 */
hal_flash_status_t hal_flash_write(uint32_t address, const uint8_t *data, uint32_t length);

/**
 * @brief Read data from flash memory (direct memory-mapped read)
 * @param address Flash address to read from
 * @param buffer Buffer to store read data
 * @param length Number of bytes to read
 */
void hal_flash_read(uint32_t address, uint8_t *buffer, uint32_t length);

/**
 * @brief Get the OTA staging bank (always Bank 2)
 * @return HAL_FLASH_BANK_2
 */
uint8_t hal_flash_get_inactive_bank(void);

/**
 * @brief Get base address of a flash bank
 * @param bank HAL_FLASH_BANK_1 or HAL_FLASH_BANK_2
 * @return Base address, or 0 if invalid bank
 */
uint32_t hal_flash_get_bank_base(uint8_t bank);

/**
 * @brief Set the firmware update flag in RTC backup registers
 *
 * Stores firmware metadata so the bootloader knows to copy from Bank 2
 * to the application area in Bank 1 on next boot.
 *
 * @param fw_size Firmware size in bytes
 * @param crc32 Expected CRC32 of the firmware
 */
void hal_flash_set_update_flag(uint32_t fw_size, uint32_t crc32);

/**
 * @brief Reset the system to trigger bootloader firmware copy
 *
 * Call after hal_flash_set_update_flag(). This function does NOT return.
 */
void hal_flash_reset_for_update(void);

/**
 * @brief Compute CRC32 over a flash memory region
 * @param address Start address
 * @param length Number of bytes
 * @return CRC32 value
 */
uint32_t hal_flash_compute_crc32(uint32_t address, uint32_t length);

#endif // HAL_FLASH_H
