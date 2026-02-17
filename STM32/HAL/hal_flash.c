/**
 * @file hal_flash.c
 * @brief Flash memory HAL implementation for STM32F767 dual-bank OTA
 *
 * Application always runs from Bank 1 (sectors 1-11, starting at 0x08004000).
 * Bank 2 is used as OTA staging. The bootloader at sector 0 copies new
 * firmware from Bank 2 → Bank 1 on reboot when the update flag is set.
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

// BOOT_ADD0 value — always Bank 1 (bootloader handles the rest)
#define BOOT_ADDR_BANK1  0x2000U  // 0x08000000 >> 14

// RTC backup register magic value for update pending
#define UPDATE_MAGIC     0xDEADBEEFU
#define BOOT_CONFIRMED_MAGIC  0xB007C0DEU

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

    // Ensure flash is in dual-bank mode (nDBANK = 0) and BOOT_ADD0 = Bank 1
    FLASH_OBProgramInitTypeDef ob_config;
    HAL_FLASHEx_OBGetConfig(&ob_config);

    bool needs_update = false;

    if (ob_config.USERConfig & FLASH_OPTCR_nDBANK) {
        LOG_W(TAG, "Flash is in SINGLE-BANK mode — need to switch to dual-bank");
        needs_update = true;
    }

    if (ob_config.BootAddr0 != BOOT_ADDR_BANK1) {
        LOG_W(TAG, "BOOT_ADD0 is 0x%04lX — fixing to 0x%04X (Bank 1)",
              (uint32_t)ob_config.BootAddr0, BOOT_ADDR_BANK1);
        needs_update = true;
    }

    if (needs_update) {
        LOG_I(TAG, "Updating option bytes...");

        HAL_FLASH_Unlock();
        HAL_FLASH_OB_Unlock();

        if (ob_config.USERConfig & FLASH_OPTCR_nDBANK) {
            ob_config.OptionType = OPTIONBYTE_USER;
            ob_config.USERConfig &= ~FLASH_OPTCR_nDBANK;
            HAL_FLASHEx_OBProgram(&ob_config);
        }

        if (ob_config.BootAddr0 != BOOT_ADDR_BANK1) {
            ob_config.OptionType = OPTIONBYTE_BOOTADDR_0;
            ob_config.BootAddr0 = BOOT_ADDR_BANK1;
            HAL_FLASHEx_OBProgram(&ob_config);
        }

        HAL_FLASH_OB_Lock();
        HAL_FLASH_Lock();

        LOG_I(TAG, "Option bytes written, launching reset...");
        HAL_FLASH_OB_Launch();
        NVIC_SystemReset();
        // Should not reach here
    }

    LOG_I(TAG, "Flash HAL initialized (dual-bank, %u sectors/bank)", SECTORS_PER_BANK);
    LOG_I(TAG, "App runs from Bank 1 (0x08008000), Bank 2 = OTA staging");

    return HAL_FL_OK;
}

hal_flash_status_t hal_flash_erase_bank(uint8_t bank)
{
    if (bank != HAL_FLASH_BANK_1 && bank != HAL_FLASH_BANK_2) {
        return HAL_FL_ERR_INVALID_ARG;
    }

    uint32_t first_sector = (bank == HAL_FLASH_BANK_2) ? BANK2_FIRST_SECTOR : 0;
    uint32_t stm32_bank = (bank == HAL_FLASH_BANK_2) ? FLASH_BANK_2 : FLASH_BANK_1;

    LOG_I(TAG, "Erasing bank %u (physical sectors %lu-%lu)...",
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
        .VoltageRange = FLASH_VOLTAGE_RANGE_3,
    };

    LOG_I(TAG, "HAL_FLASHEx_Erase: Banks=0x%08lX, Sector=%lu, NbSectors=%lu",
          erase_init.Banks, erase_init.Sector, erase_init.NbSectors);

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

    if (address & 0x03) {
        LOG_E(TAG, "Write address 0x%08lX not word-aligned", address);
        return HAL_FL_ERR_ALIGNMENT;
    }

    HAL_StatusTypeDef status = HAL_FLASH_Unlock();
    if (status != HAL_OK) {
        return HAL_FL_ERR_LOCK;
    }

    hal_flash_status_t result = HAL_FL_OK;

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
    memcpy(buffer, (const void *)address, length);
}

uint8_t hal_flash_get_inactive_bank(void)
{
    // With bootloader approach, OTA staging is always Bank 2
    return HAL_FLASH_BANK_2;
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

void hal_flash_set_update_flag(uint32_t fw_size, uint32_t crc32,
                               uint8_t version_major, uint8_t version_minor, uint8_t version_patch)
{
    LOG_I(TAG, "Setting update flag: v%u.%u.%u, size=%lu, CRC=0x%08lX",
          version_major, version_minor, version_patch, fw_size, crc32);

    // Enable backup domain access
    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();
    __HAL_RCC_RTC_ENABLE();

    // Write firmware metadata to RTC backup registers
    RTC->BKP1R = fw_size;
    RTC->BKP2R = crc32;

    // Store new firmware version (packed: major<<16 | minor<<8 | patch)
    uint32_t packed_version = (version_major << 16) | (version_minor << 8) | version_patch;
    RTC->BKP4R = packed_version;

    RTC->BKP0R = UPDATE_MAGIC;  // Write flag last (atomic commit)

    LOG_I(TAG, "Update flag set — bootloader will copy on next boot");
}

void hal_flash_reset_for_update(void)
{
    LOG_I(TAG, "Resetting for firmware update...");
    HAL_Delay(50);  // Allow UART TX to flush
    NVIC_SystemReset();
    // Does not return
    while (1) {}
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

void hal_flash_confirm_boot(void)
{
    // Enable backup domain access
    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();
    __HAL_RCC_RTC_ENABLE();

    // Write confirmation magic to RTC backup register
    RTC->BKP5R = BOOT_CONFIRMED_MAGIC;

    LOG_I(TAG, "Boot confirmed — bootloader will reset attempt counter on next boot");
}

uint32_t hal_flash_get_boot_attempts(void)
{
    // Enable backup domain access (read-only, safe to call anytime)
    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();
    __HAL_RCC_RTC_ENABLE();

    return RTC->BKP3R;
}

// ============================================================================
// Watchdog Functions
// ============================================================================

#define IWDG_KEY_REFRESH    0xAAAAU

void hal_watchdog_kick(void)
{
    /* Refresh IWDG counter (prevents reset) */
    IWDG->KR = IWDG_KEY_REFRESH;
}

bool hal_watchdog_is_active(void)
{
    /* Check if IWDG is running by reading the status register
     * If IWDG was never started, these registers are typically 0 */
    return (IWDG->PR != 0 || IWDG->RLR != 0);
}
