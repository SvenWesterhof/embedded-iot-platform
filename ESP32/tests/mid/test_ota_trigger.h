/**
 * @file test_ota_trigger.h
 * @brief OTA test trigger (for development only)
 */

#ifndef TEST_OTA_TRIGGER_H
#define TEST_OTA_TRIGGER_H

#include <stdbool.h>

/**
 * @brief Trigger a test OTA update
 * @param url HTTPS URL to firmware binary
 * @param version Version string
 * @param auto_reboot Auto-reboot after update
 */
void test_ota_trigger(const char *url, const char *version, bool auto_reboot);

/**
 * @brief Example: Test with public ESP32 example firmware
 */
void test_ota_example(void);

#endif // TEST_OTA_TRIGGER_H
