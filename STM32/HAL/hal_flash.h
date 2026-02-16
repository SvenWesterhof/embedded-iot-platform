#ifndef HAL_FLASH_H
#define HAL_FLASH_H

/**
 * @file hal_flash.h
 * @brief Flash memory abstraction layer for dual-bank OTA
 *
 * Provides flash erase, write, read, and bank swap operations
 * for STM32F767 dual-bank flash (2x 1MB).
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
 *
 * Handles unlock/lock and writes in 32-bit words.
 * Data length must be a multiple of 4, or the last partial word
 * will be padded with 0xFF.
 *
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
 * @brief Get which flash bank the application is currently running from
 * @return HAL_FLASH_BANK_1 or HAL_FLASH_BANK_2
 */
uint8_t hal_flash_get_active_bank(void);

/**
 * @brief Get the inactive bank (the one NOT currently running)
 * @return HAL_FLASH_BANK_1 or HAL_FLASH_BANK_2
 */
uint8_t hal_flash_get_inactive_bank(void);

/**
 * @brief Get base address of a flash bank
 * @param bank HAL_FLASH_BANK_1 or HAL_FLASH_BANK_2
 * @return Base address, or 0 if invalid bank
 */
uint32_t hal_flash_get_bank_base(uint8_t bank);

/**
 * @brief Swap boot address to the other bank and reset
 *
 * Changes BOOT_ADD0 option byte to point to the inactive bank,
 * then triggers a system reset. This function does NOT return.
 *
 * @return HAL_FL_OK only if swap failed (should not happen)
 */
hal_flash_status_t hal_flash_swap_bank_and_reset(void);

/**
 * @brief Compute CRC32 over a flash memory region
 * @param address Start address
 * @param length Number of bytes
 * @return CRC32 value
 */
uint32_t hal_flash_compute_crc32(uint32_t address, uint32_t length);

#endif // HAL_FLASH_H
