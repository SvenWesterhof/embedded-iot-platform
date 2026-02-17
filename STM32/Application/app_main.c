#include "app_main.h"
#include "services.h"
#include "event_bus.h"
#include "portable_log.h"
#include "hal_flash.h"
#include "config.h"

static const char *TAG = "APP";

/* Firmware version — embedded for OTA bootloader tracking */
const uint32_t g_firmware_version = FW_VERSION_PACKED;

void app_init(void)
{
    // Initialize event bus first
    event_bus_init();
    LOG_I(TAG, "Event bus initialized");

    // Initialize services (including display service which subscribes to events)
    services_init();
    LOG_I(TAG, "Services initialized");

    LOG_I(TAG, "Application initialized successfully");
    LOG_I(TAG, "Firmware version: v%d.%d.%d", 
          APP_VERSION_MAJOR, APP_VERSION_MINOR, APP_VERSION_PATCH);

    /* Confirm successful boot to bootloader — CRITICAL for anti-brick mechanism
     * Bootloader tracks boot attempts. If we don't confirm after MAX_BOOT_ATTEMPTS,
     * it knows this firmware is bad. Call this AFTER all critical init is done. */
    hal_flash_confirm_boot();
}

void app_run(void)
{
    
    // Run services (they publish events)
    services_run();
    // Process any pending events
    event_bus_process();
    
}
