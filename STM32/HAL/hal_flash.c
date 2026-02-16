/**
 * @file hal_flash.c
 * @brief Flash memory HAL implementation for STM32F767 dual-bank OTA
 */

#include "hal_flash.h"
#include "stm32f7xx_hal.h"
#include "portable_log.h"
#include <string.h>

static const char *TAG = "HAL_FLASH";

// Dual-bank mode: 12 sectors per bank
// Bank 1: sectors 0-11,  Bank 2: sectors 12-23
#define SECTORS_PER_BANK     12
#define BANK2_FIRST_SECTOR   12

// BOOT_ADD0 option byte values (shifted >> 14 to get address)
// 0x2000 = 0x08000000 (Bank 1 via AXIM), 0x2040 = 0x08100000 (Bank 2 via AXIM)
#define BOOT_ADDR_BANK1  0x2000U
#define BOOT_ADDR_BANK2  0x2040U

// ============================================================================
// CRC32 (software implementation — standard polynomial 0x04C11DB7)
// ============================================================================

static uint32_t crc32_table[256];
static bool crc32_table_initialized = false;

static void crc32_init_table(void)
{
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320U;  // Reflected polynomial
            } else {
                crc >>= 1;
            }
        }
        crc32_table[i] = crc;
    }
    crc32_table_initialized = true;
}

// ============================================================================
// Public API
// ============================================================================

hal_flash_status_t hal_flash_init(void)
{
    if (!crc32_table_initialized) {
        crc32_init_table();
    }
    LOG_I(TAG, "Flash HAL initialized (dual-bank, %u sectors/bank)", SECTORS_PER_BANK);
    return HAL_FL_OK;
}

hal_flash_status_t hal_flash_erase_bank(uint8_t bank)
{
    if (bank != HAL_FLASH_BANK_1 && bank != HAL_FLASH_BANK_2) {
        return HAL_FL_ERR_INVALID_ARG;
    }

    uint32_t first_sector = (bank == HAL_FLASH_BANK_2) ? BANK2_FIRST_SECTOR : 0;
    uint32_t stm32_bank = (bank == HAL_FLASH_BANK_2) ? FLASH_BANK_2 : FLASH_BANK_1;

    LOG_I(TAG, "Erasing bank %u (sectors %lu-%lu)...",
          bank, first_sector, first_sector + SECTORS_PER_BANK - 1);

    HAL_StatusTypeDef status = HAL_FLASH_Unlock();
    if (status != HAL_OK) {
        LOG_E(TAG, "Flash unlock failed: %d", status);
        return HAL_FL_ERR_LOCK;
    }

    FLASH_EraseInitTypeDef erase_init = {
        .TypeErase = FLASH_TYPEERASE_SECTORS,
        .Banks = stm32_bank,
        .Sector = first_sector,
        .NbSectors = SECTORS_PER_BANK,
        .VoltageRange = FLASH_VOLTAGE_RANGE_3,  // 2.7V - 3.6V
    };

    uint32_t sector_error = 0;
    status = HAL_FLASHEx_Erase(&erase_init, &sector_error);

    HAL_FLASH_Lock();

    if (status != HAL_OK) {
        LOG_E(TAG, "Bank %u erase failed at sector %lu (HAL status %d)",
              bank, sector_error, status);
        return HAL_FL_ERR_ERASE;
    }

    LOG_I(TAG, "Bank %u erased successfully", bank);
    return HAL_FL_OK;
}

hal_flash_status_t hal_flash_write(uint32_t address, const uint8_t *data, uint32_t length)
{
    if (data == NULL || length == 0) {
        return HAL_FL_ERR_INVALID_ARG;
    }

    // Address must be word-aligned
    if (address & 0x03) {
        LOG_E(TAG, "Write address 0x%08lX not word-aligned", address);
        return HAL_FL_ERR_ALIGNMENT;
    }

    HAL_StatusTypeDef status = HAL_FLASH_Unlock();
    if (status != HAL_OK) {
        return HAL_FL_ERR_LOCK;
    }

    hal_flash_status_t result = HAL_FL_OK;

    // Write complete 32-bit words
    uint32_t words = length / 4;
    uint32_t remainder = length % 4;

    for (uint32_t i = 0; i < words; i++) {
        uint32_t word;
        memcpy(&word, data + (i * 4), 4);

        status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                                   address + (i * 4), word);
        if (status != HAL_OK) {
            LOG_E(TAG, "Flash write failed at 0x%08lX (HAL status %d)",
                  address + (i * 4), status);
            result = HAL_FL_ERR_WRITE;
            break;
        }
    }

    // Handle remaining bytes (pad with 0xFF)
    if (result == HAL_FL_OK && remainder > 0) {
        uint32_t last_word = 0xFFFFFFFF;
        memcpy(&last_word, data + (words * 4), remainder);

        status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                                   address + (words * 4), last_word);
        if (status != HAL_OK) {
            LOG_E(TAG, "Flash write (remainder) failed");
            result = HAL_FL_ERR_WRITE;
        }
    }

    HAL_FLASH_Lock();
    return result;
}

void hal_flash_read(uint32_t address, uint8_t *buffer, uint32_t length)
{
    // Flash is memory-mapped, direct copy
    memcpy(buffer, (const void *)address, length);
}

uint8_t hal_flash_get_active_bank(void)
{
    FLASH_OBProgramInitTypeDef ob_config;
    HAL_FLASHEx_OBGetConfig(&ob_config);

    // BOOT_ADD0 contains the boot address shifted right by 14
    // Bank 1: 0x08000000 >> 14 = 0x2000
    // Bank 2: 0x08100000 >> 14 = 0x2040
    uint32_t boot_addr = ob_config.BootAddr0;

    if (boot_addr == BOOT_ADDR_BANK2) {
        return HAL_FLASH_BANK_2;
    }
    return HAL_FLASH_BANK_1;  // Default to Bank 1
}

uint8_t hal_flash_get_inactive_bank(void)
{
    return (hal_flash_get_active_bank() == HAL_FLASH_BANK_1)
           ? HAL_FLASH_BANK_2 : HAL_FLASH_BANK_1;
}

uint32_t hal_flash_get_bank_base(uint8_t bank)
{
    if (bank == HAL_FLASH_BANK_2) {
        return HAL_FLASH_BANK2_BASE;
    }
    if (bank == HAL_FLASH_BANK_1) {
        return HAL_FLASH_BANK1_BASE;
    }
    return 0;
}

hal_flash_status_t hal_flash_swap_bank_and_reset(void)
{
    uint8_t active = hal_flash_get_active_bank();
    uint32_t new_boot_addr = (active == HAL_FLASH_BANK_1)
                             ? BOOT_ADDR_BANK2 : BOOT_ADDR_BANK1;

    LOG_I(TAG, "Swapping boot from bank %u to bank %u",
          active, (active == HAL_FLASH_BANK_1) ? 2 : 1);

    HAL_StatusTypeDef status = HAL_FLASH_Unlock();
    if (status != HAL_OK) {
        LOG_E(TAG, "Flash unlock failed for OB write");
        return HAL_FL_ERR_LOCK;
    }

    status = HAL_FLASH_OB_Unlock();
    if (status != HAL_OK) {
        HAL_FLASH_Lock();
        LOG_E(TAG, "OB unlock failed");
        return HAL_FL_ERR_LOCK;
    }

    // Read current option bytes to preserve existing settings (including RDP!)
    FLASH_OBProgramInitTypeDef ob_config;
    HAL_FLASHEx_OBGetConfig(&ob_config);

    // Only change the boot address
    ob_config.OptionType = OPTIONBYTE_BOOTADDR_0;
    ob_config.BootAddr0 = new_boot_addr;

    status = HAL_FLASHEx_OBProgram(&ob_config);
    if (status != HAL_OK) {
        HAL_FLASH_OB_Lock();
        HAL_FLASH_Lock();
        LOG_E(TAG, "OB program failed: %d", status);
        return HAL_FL_ERR_WRITE;
    }

    LOG_I(TAG, "Boot address updated. Resetting...");

    // Launch option bytes and reset
    HAL_FLASH_OB_Launch();
    // OB_Launch triggers a system reset — should not reach here
    NVIC_SystemReset();

    // Should never reach here
    return HAL_FL_OK;
}

uint32_t hal_flash_compute_crc32(uint32_t address, uint32_t length)
{
    if (!crc32_table_initialized) {
        crc32_init_table();
    }

    const uint8_t *data = (const uint8_t *)address;
    uint32_t crc = 0xFFFFFFFF;

    for (uint32_t i = 0; i < length; i++) {
        crc = crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }

    return crc ^ 0xFFFFFFFF;
}
