/**
 * @file test_ota_trigger.c
 * @brief OTA test trigger (for development only)
 */

#include "../Middleware/Control/cont_ota_manager.h"
#include "portable_log.h"

static const char *TAG = "OTA_TEST";

/**
 * @brief Trigger a test OTA update
 * @param url HTTPS URL to firmware binary
 * @param version Version string
 * @param auto_reboot Auto-reboot after update
 */
void test_ota_trigger(const char *url, const char *version, bool auto_reboot)
{
    LOG_I(TAG, "=== OTA Test Trigger ===");
    LOG_I(TAG, "URL: %s", url);
    LOG_I(TAG, "Version: %s", version);
    LOG_I(TAG, "Auto-reboot: %s", auto_reboot ? "yes" : "no");

    ota_notification_t notification = {
        .version = version,
        .https_url = url,
        .expected_size = 0,  // Auto-detect from Content-Length header
        .auto_reboot = auto_reboot
    };

    ota_mgr_status_t status = cont_ota_trigger_update(&notification);
    if (status == OTA_MGR_OK) {
        LOG_I(TAG, "✓ OTA update triggered successfully");
        LOG_I(TAG, "Watch logs for download progress...");
    } else {
        LOG_E(TAG, "✗ Failed to trigger OTA: error %d", status);
    }
}

/**
 * @brief Example: Test with public ESP32 example firmware
 */
void test_ota_example(void)
{
    // Example using ESP32 sample OTA server
    test_ota_trigger(
        "https://esp32.com/viewtopic.php?t=12345",  // Replace with actual URL
        "1.1.0",
        false  // Don't auto-reboot (manual reboot for safety)
    );
}
