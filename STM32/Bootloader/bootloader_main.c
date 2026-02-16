/**
 * @file bootloader_main.c
 * @brief Minimal bootloader for STM32F767 dual-bank OTA
 *
 * Occupies Sectors 0-1 of Bank 1 (32KB at 0x08000000).
 * On reset:
 *   1. Checks RTC backup registers for "update pending" flag
 *   2. If pending: copies firmware from Bank 2 staging → Bank 1 app area
 *   3. Validates the app vector table
 *   4. Jumps to the application at 0x08008000
 *
 * Uses STM32 HAL for flash operations (proven, reliable).
 * USART3 printf for debug output (ST-Link VCP at 115200 baud).
 */

#include "stm32f7xx_hal.h"
#include <stdio.h>
#include <stdbool.h>

/* ========================================================================== */
/* Constants                                                                   */
/* ========================================================================== */

#define APP_ADDRESS             0x08008000U   /* Application start (Sector 2) */
#define BANK2_BASE              0x08100000U   /* OTA staging area */
#define BANK1_APP_MAX_SIZE      0x000F8000U   /* 992KB (sectors 2-11) */

#define UPDATE_MAGIC            0xDEADBEEFU   /* RTC_BKP0R: update pending */

/* RTC Backup Register assignments */
#define BKP_UPDATE_FLAG         RTC->BKP0R    /* Magic value = update pending */
#define BKP_FW_SIZE             RTC->BKP1R    /* Firmware size in bytes */
#define BKP_FW_CRC              RTC->BKP2R    /* Expected CRC32 */

/* Dual-bank sector layout */
#define BANK1_FIRST_APP_SECTOR  2             /* First app sector (after bootloader) */
#define BANK1_LAST_SECTOR       11            /* Last sector in Bank 1 */
#define SECTORS_TO_ERASE        (BANK1_LAST_SECTOR - BANK1_FIRST_APP_SECTOR + 1)

/* BOOT_ADD0 values */
#define BOOT_ADDR_BANK1         0x2000U       /* 0x08000000 >> 14 */

/* ========================================================================== */
/* UART debug output                                                           */
/* ========================================================================== */

static UART_HandleTypeDef huart3_bl;

/* Raw UART output — works even when printf/newlib is broken */
static void bl_raw_putc(char c)
{
    while (!(USART3->ISR & USART_ISR_TXE)) {}
    USART3->TDR = (uint8_t)c;
}

static void bl_raw_puts(const char *s)
{
    for (; *s; s++) {
        if (*s == '\n') bl_raw_putc('\r');
        bl_raw_putc(*s);
    }
}

static void bl_raw_hex32(uint32_t val)
{
    static const char hex[] = "0123456789ABCDEF";
    bl_raw_puts("0x");
    for (int i = 28; i >= 0; i -= 4) {
        bl_raw_putc(hex[(val >> i) & 0xF]);
    }
}

static void bl_raw_flush(void)
{
    while (!(USART3->ISR & USART_ISR_TC)) {}
}

/* Print a labeled hex value: "  label = 0x12345678\r\n" */
static void bl_raw_print_reg(const char *label, uint32_t val)
{
    bl_raw_puts("  ");
    bl_raw_puts(label);
    bl_raw_puts(" = ");
    bl_raw_hex32(val);
    bl_raw_puts("\n");
}

static void bl_uart_init(void)
{
    __HAL_RCC_USART3_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();

    /* USART3: PD8 (TX), PD9 (RX) — ST-Link VCP on Nucleo-F767ZI */
    GPIO_InitTypeDef gpio = {
        .Pin       = GPIO_PIN_8 | GPIO_PIN_9,
        .Mode      = GPIO_MODE_AF_PP,
        .Pull      = GPIO_NOPULL,
        .Speed     = GPIO_SPEED_FREQ_VERY_HIGH,
        .Alternate = GPIO_AF7_USART3,
    };
    HAL_GPIO_Init(GPIOD, &gpio);

    huart3_bl.Instance          = USART3;
    huart3_bl.Init.BaudRate     = 115200;
    huart3_bl.Init.WordLength   = UART_WORDLENGTH_8B;
    huart3_bl.Init.StopBits     = UART_STOPBITS_1;
    huart3_bl.Init.Parity       = UART_PARITY_NONE;
    huart3_bl.Init.Mode         = UART_MODE_TX_RX;
    huart3_bl.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart3_bl.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&huart3_bl);
}

int __io_putchar(int ch)
{
    HAL_UART_Transmit(&huart3_bl, (uint8_t *)&ch, 1, HAL_MAX_DELAY);
    return ch;
}

/* ========================================================================== */
/* CRC32 (same algorithm as application — standard polynomial 0x04C11DB7)      */
/* ========================================================================== */

static uint32_t crc32_table[256];

static void crc32_init_table(void)
{
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xEDB88320U;
            else
                crc >>= 1;
        }
        crc32_table[i] = crc;
    }
}

static uint32_t crc32_compute(const uint8_t *data, uint32_t length)
{
    uint32_t crc = 0xFFFFFFFF;
    for (uint32_t i = 0; i < length; i++) {
        crc = crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;
}

/* ========================================================================== */
/* Backup domain access                                                        */
/* ========================================================================== */

static void bl_enable_backup_domain(void)
{
    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();
    __HAL_RCC_RTC_ENABLE();
}

/* ========================================================================== */
/* BOOT_ADD0 enforcement                                                       */
/* ========================================================================== */

static void bl_ensure_boot_addr_bank1(void)
{
    FLASH_OBProgramInitTypeDef ob;
    HAL_FLASHEx_OBGetConfig(&ob);

    printf("[BL] Current OB: BOOT_ADD0=0x%04lX, nDBANK=%lu, USERConfig=0x%08lX\n",
           (uint32_t)ob.BootAddr0,
           (ob.USERConfig & FLASH_OPTCR_nDBANK) ? 1UL : 0UL,
           (uint32_t)ob.USERConfig);

    bool needs_update = false;

    /* Ensure dual-bank mode (nDBANK=0) */
    if (ob.USERConfig & FLASH_OPTCR_nDBANK) {
        needs_update = true;
    }

    /* Ensure BOOT_ADD0 points to Bank 1 */
    if (ob.BootAddr0 != BOOT_ADDR_BANK1) {
        needs_update = true;
    }

    if (!needs_update) {
        printf("[BL] Option bytes are correct, no update needed\n");
        return;
    }

    printf("[BL] Fixing option bytes...\n");

    HAL_FLASH_Unlock();
    HAL_FLASH_OB_Unlock();

    /* Fix dual-bank mode if needed */
    if (ob.USERConfig & FLASH_OPTCR_nDBANK) {
        ob.OptionType = OPTIONBYTE_USER;
        ob.USERConfig &= ~FLASH_OPTCR_nDBANK;
        HAL_FLASHEx_OBProgram(&ob);
    }

    /* Fix BOOT_ADD0 if needed */
    if (ob.BootAddr0 != BOOT_ADDR_BANK1) {
        ob.OptionType = OPTIONBYTE_BOOTADDR_0;
        ob.BootAddr0  = BOOT_ADDR_BANK1;
        HAL_FLASHEx_OBProgram(&ob);
    }

    HAL_FLASH_OB_Lock();
    HAL_FLASH_Lock();

    printf("[BL] Option bytes updated, resetting...\n");
    HAL_FLASH_OB_Launch();
    NVIC_SystemReset();
    while (1) {}  /* Should not reach here */
}

/* ========================================================================== */
/* Flash operations                                                            */
/* ========================================================================== */

static bool bl_erase_app_sectors(void)
{
    printf("[BL] Erasing Bank 1 sectors %d-%d...\n",
           BANK1_FIRST_APP_SECTOR, BANK1_LAST_SECTOR);

    HAL_FLASH_Unlock();

    FLASH_EraseInitTypeDef erase = {
        .TypeErase    = FLASH_TYPEERASE_SECTORS,
        .Banks        = FLASH_BANK_1,
        .Sector       = BANK1_FIRST_APP_SECTOR,
        .NbSectors    = SECTORS_TO_ERASE,
        .VoltageRange = FLASH_VOLTAGE_RANGE_3,
    };

    uint32_t error = 0;
    HAL_StatusTypeDef status = HAL_FLASHEx_Erase(&erase, &error);

    HAL_FLASH_Lock();

    if (status != HAL_OK) {
        printf("[BL] Erase FAILED at sector %lu (HAL status %d)\n", error, status);
        return false;
    }

    printf("[BL] Erase complete\n");
    return true;
}

static bool bl_copy_firmware(uint32_t fw_size)
{
    printf("[BL] Copying %lu bytes: 0x%08lX -> 0x%08lX\n",
           fw_size, (uint32_t)BANK2_BASE, (uint32_t)APP_ADDRESS);

    HAL_FLASH_Unlock();

    const uint32_t *src = (const uint32_t *)BANK2_BASE;
    uint32_t dst_addr = APP_ADDRESS;
    uint32_t words = (fw_size + 3) / 4;  /* Round up to full words */

    for (uint32_t i = 0; i < words; i++) {
        HAL_StatusTypeDef status = HAL_FLASH_Program(
            FLASH_TYPEPROGRAM_WORD, dst_addr, src[i]);

        if (status != HAL_OK) {
            printf("[BL] Write FAILED at 0x%08lX (HAL status %d)\n",
                   dst_addr, status);
            HAL_FLASH_Lock();
            return false;
        }

        dst_addr += 4;

        /* Progress every 64KB */
        if ((i & 0x3FFF) == 0 && i > 0) {
            uint32_t pct = (i * 4 * 100) / fw_size;
            printf("[BL] Copy progress: %lu%%\n", pct);
        }
    }

    HAL_FLASH_Lock();
    printf("[BL] Copy complete\n");
    return true;
}

static bool bl_verify_crc(uint32_t fw_size, uint32_t expected_crc)
{
    printf("[BL] Verifying CRC32 over %lu bytes at 0x%08lX...\n",
           fw_size, (uint32_t)APP_ADDRESS);

    uint32_t computed = crc32_compute((const uint8_t *)APP_ADDRESS, fw_size);

    printf("[BL] CRC32: computed=0x%08lX, expected=0x%08lX\n",
           computed, expected_crc);

    return (computed == expected_crc);
}

/* ========================================================================== */
/* Firmware update from Bank 2 staging                                         */
/* ========================================================================== */

static bool bl_apply_update(void)
{
    uint32_t fw_size     = BKP_FW_SIZE;
    uint32_t expected_crc = BKP_FW_CRC;

    printf("[BL] Update pending: size=%lu, CRC=0x%08lX\n", fw_size, expected_crc);

    /* Validate size */
    if (fw_size == 0 || fw_size > BANK1_APP_MAX_SIZE) {
        printf("[BL] ERROR: invalid firmware size\n");
        return false;
    }

    /* Erase application sectors */
    if (!bl_erase_app_sectors()) {
        return false;
    }

    /* Copy from Bank 2 staging to Bank 1 application area */
    if (!bl_copy_firmware(fw_size)) {
        return false;
    }

    /* Verify CRC of the copied firmware */
    if (!bl_verify_crc(fw_size, expected_crc)) {
        printf("[BL] ERROR: CRC mismatch after copy!\n");
        return false;
    }

    /* Clear the update flag */
    bl_enable_backup_domain();
    BKP_UPDATE_FLAG = 0x00000000;

    printf("[BL] Update applied successfully\n");
    return true;
}

/* ========================================================================== */
/* Application validation and jump                                             */
/* ========================================================================== */

static bool bl_validate_app(void)
{
    uint32_t app_sp = *(volatile uint32_t *)APP_ADDRESS;
    uint32_t app_pc = *(volatile uint32_t *)(APP_ADDRESS + 4);

    printf("[BL] App vector table: SP=0x%08lX, PC=0x%08lX\n", app_sp, app_pc);

    /* SP must point to RAM (0x20000000 - 0x20080000) */
    if (app_sp < 0x20000000 || app_sp > 0x20080000) {
        printf("[BL] ERROR: Invalid SP (not in RAM)\n");
        return false;
    }

    /* Reset_Handler must point to app flash region */
    if (app_pc < APP_ADDRESS || app_pc >= (APP_ADDRESS + BANK1_APP_MAX_SIZE)) {
        printf("[BL] ERROR: Invalid Reset_Handler (not in app flash)\n");
        return false;
    }

    return true;
}

static void bl_jump_to_app(void) __attribute__((noreturn));
static void bl_jump_to_app(void)
{
    printf("[BL] Jumping to application at 0x%08lX\n\n", (uint32_t)APP_ADDRESS);

    /* Flush UART before jumping */
    HAL_Delay(10);

    /* Disable all interrupts */
    __disable_irq();

    /* Reset peripherals used by bootloader */
    HAL_UART_DeInit(&huart3_bl);
    HAL_RCC_DeInit();
    HAL_DeInit();

    /* Clear pending interrupts */
    for (int i = 0; i < 8; i++) {
        NVIC->ICER[i] = 0xFFFFFFFF;
        NVIC->ICPR[i] = 0xFFFFFFFF;
    }

    /* Set VTOR to application */
    SCB->VTOR = APP_ADDRESS;

    /* Load SP and PC from application vector table */
    uint32_t app_sp = *(volatile uint32_t *)APP_ADDRESS;
    uint32_t app_pc = *(volatile uint32_t *)(APP_ADDRESS + 4);

    /* Set MSP and jump */
    __set_MSP(app_sp);
    __DSB();
    __ISB();

    void (*app_entry)(void) = (void (*)(void))app_pc;
    app_entry();

    /* Should never reach here */
    while (1) {}
}

/* ========================================================================== */
/* Error handler — blink LED forever                                           */
/* ========================================================================== */

static void bl_error_blink(void)
{
    /* PF13 = external LED on the user's board */
    __HAL_RCC_GPIOF_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {
        .Pin  = GPIO_PIN_13,
        .Mode = GPIO_MODE_OUTPUT_PP,
        .Pull = GPIO_NOPULL,
        .Speed = GPIO_SPEED_FREQ_LOW,
    };
    HAL_GPIO_Init(GPIOF, &gpio);

    printf("[BL] ERROR: Halted. Blinking LED.\n");
    while (1) {
        HAL_GPIO_TogglePin(GPIOF, GPIO_PIN_13);
        HAL_Delay(200);
    }
}

/* ========================================================================== */
/* Reset cause detection                                                       */
/* ========================================================================== */

static void bl_print_reset_cause(void)
{
    uint32_t csr = RCC->CSR;

    printf("[BL] Reset cause:");
    if (csr & RCC_CSR_LPWRRSTF)  printf(" LOW-POWER");
    if (csr & RCC_CSR_WWDGRSTF)  printf(" WWDG");
    if (csr & RCC_CSR_IWDGRSTF)  printf(" IWDG");
    if (csr & RCC_CSR_SFTRSTF)   printf(" SOFTWARE");
    if (csr & RCC_CSR_PORRSTF)   printf(" POWER-ON");
    if (csr & RCC_CSR_PINRSTF)   printf(" PIN-RESET");
    if (csr & RCC_CSR_BORRSTF)   printf(" BROWNOUT");
    printf("\n");

    /* Clear reset flags so next reset shows only the new cause */
    __HAL_RCC_CLEAR_RESET_FLAGS();
}

/* ========================================================================== */
/* HardFault diagnostic handler                                                */
/* ========================================================================== */

/**
 * Called from HardFault_Handler with the correct stack pointer.
 * Prints the stacked registers and fault status registers.
 */
void bl_hardfault_diagnostic(uint32_t *stack_frame)
{
    /* Re-init UART clocks directly — HAL state may be corrupted */
    __HAL_RCC_USART3_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();

    /* ALL output uses raw UART — printf is NOT safe in a fault handler */
    bl_raw_puts("\n\n!!! HARD FAULT !!!\n");
    bl_raw_print_reg("PC ", stack_frame[6]);
    bl_raw_print_reg("LR ", stack_frame[5]);
    bl_raw_print_reg("R0 ", stack_frame[0]);
    bl_raw_print_reg("R1 ", stack_frame[1]);
    bl_raw_print_reg("R2 ", stack_frame[2]);
    bl_raw_print_reg("R3 ", stack_frame[3]);
    bl_raw_print_reg("R12", stack_frame[4]);
    bl_raw_print_reg("PSR", stack_frame[7]);
    bl_raw_print_reg("CFSR ", SCB->CFSR);
    bl_raw_print_reg("HFSR ", SCB->HFSR);
    bl_raw_print_reg("MMFAR", SCB->MMFAR);
    bl_raw_print_reg("BFAR ", SCB->BFAR);

    uint32_t cfsr = SCB->CFSR;
    uint32_t hfsr = SCB->HFSR;
    if (cfsr & 0x00FF)     bl_raw_puts("  -> MemManage fault\n");
    if (cfsr & 0xFF00)     bl_raw_puts("  -> BusFault\n");
    if (cfsr & 0xFFFF0000) bl_raw_puts("  -> UsageFault\n");
    if (hfsr & SCB_HFSR_FORCED_Msk) bl_raw_puts("  -> Forced (escalated)\n");

    bl_raw_puts("\nHalted.\n");
    bl_raw_flush();

    /* Hard loop — do NOT call bl_error_blink (needs HAL_Delay/SysTick) */
    while (1) {
        __NOP();
    }
}

/**
 * HardFault_Handler — naked function to extract the correct stack pointer
 * and pass it to the C diagnostic function.
 */
void HardFault_Handler(void) __attribute__((naked));
void HardFault_Handler(void)
{
    __asm volatile(
        "tst lr, #4            \n"   /* Check EXC_RETURN bit 2 */
        "ite eq                \n"
        "mrseq r0, msp         \n"   /* Using MSP */
        "mrsne r0, psp         \n"   /* Using PSP */
        "b bl_hardfault_diagnostic \n"
    );
}

/* ========================================================================== */
/* Main                                                                        */
/* ========================================================================== */

int main(void)
{
    /* Basic system init (HSI 16 MHz, no PLL) */
    HAL_Init();

    /* Init UART for debug output */
    bl_uart_init();

    /* Print reset cause and banner */
    bl_print_reset_cause();
    printf("\n=== STM32F767 Dual-Bank Bootloader v1.0 ===\n\n");

    /* Enable backup domain for RTC backup registers */
    printf("[BL] Enabling backup domain...\n");
    bl_enable_backup_domain();

    /* Ensure BOOT_ADD0 always points to Bank 1 (one-time check) */
    printf("[BL] Checking option bytes...\n");
    bl_ensure_boot_addr_bank1();

    /* Initialize CRC table */
    crc32_init_table();

    /* Check for pending firmware update */
    uint32_t update_flag = BKP_UPDATE_FLAG;

    if (update_flag == UPDATE_MAGIC) {
        printf("[BL] Firmware update pending — applying...\n");

        if (!bl_apply_update()) {
            printf("[BL] Update FAILED\n");

            /* Clear the flag to avoid infinite retry loop */
            BKP_UPDATE_FLAG = 0x00000000;

            /* If there's still a valid app, try to boot it anyway */
            if (bl_validate_app()) {
                printf("[BL] Old app may still be valid, attempting boot...\n");
                bl_jump_to_app();
            }

            /* No valid app — halt */
            bl_error_blink();
        }
    } else {
        printf("[BL] No update pending (flag=0x%08lX)\n", update_flag);
    }

    /* Validate application */
    if (!bl_validate_app()) {
        printf("[BL] No valid application found!\n");
        bl_error_blink();
    }

    /* Jump to application */
    bl_jump_to_app();
}

/* Variables normally provided by system_stm32f7xx.c */
uint32_t SystemCoreClock = 16000000U;  /* HSI default */
const uint8_t AHBPrescTable[16] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 6, 7, 8, 9};
const uint8_t APBPrescTable[8]  = {0, 0, 0, 0, 1, 2, 3, 4};

/* Override SystemInit — bootloader doesn't need full clock setup */
void SystemInit(void)
{
    /* Enable FPU (required by HAL even at HSI clock) */
    SCB->CPACR |= ((3UL << 10*2) | (3UL << 11*2));

    /* Use default HSI (16 MHz) — fast boot, no PLL config needed */
}

/* HAL_MspInit stub — required by HAL_Init() */
void HAL_MspInit(void)
{
    /* Bootloader doesn't need MSP init */
}

/* SysTick handler — required by HAL default timebase (no FreeRTOS in bootloader) */
void SysTick_Handler(void)
{
    HAL_IncTick();
}

/* Error_Handler stub — required by HAL timebase and other HAL code */
void Error_Handler(void)
{
    printf("[BL] Error_Handler called\n");
    bl_error_blink();
}
