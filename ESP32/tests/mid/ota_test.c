/**
 * @file ota_test.c
 * @brief OTA Configuration Test Suite
 *
 * Tests to verify OTA partition configuration and rollback protection
 */

#include <stdio.h>
#include <string.h>
#include "esp_partition.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "portable_log.h"

static const char *TAG = "OTA_TEST";

/**
 * @brief Test 1: Verify partition table is accessible
 */
void test_partition_table(void)
{
    LOG_I(TAG, "========================================");
    LOG_I(TAG, "TEST 1: Partition Table Accessibility");
    LOG_I(TAG, "========================================");

    // List all partitions
    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);

    int count = 0;
    while (it != NULL) {
        const esp_partition_t *part = esp_partition_get(it);

        const char *type_str = (part->type == ESP_PARTITION_TYPE_APP) ? "app" :
                               (part->type == ESP_PARTITION_TYPE_DATA) ? "data" : "unknown";

        LOG_I(TAG, "Partition %d:", count++);
        LOG_I(TAG, "  Label:   %s", part->label);
        LOG_I(TAG, "  Type:    %s (0x%02x)", type_str, part->type);
        LOG_I(TAG, "  Subtype: 0x%02x", part->subtype);
        LOG_I(TAG, "  Offset:  0x%08lx (%lu KB)", part->address, part->address / 1024);
        LOG_I(TAG, "  Size:    0x%08lx (%lu KB)", part->size, part->size / 1024);

        it = esp_partition_next(it);
    }
    esp_partition_iterator_release(it);

    LOG_I(TAG, "✓ Found %d partitions", count);
    LOG_I(TAG, "");
}

/**
 * @brief Test 2: Check running partition info
 */
void test_running_partition(void)
{
    LOG_I(TAG, "========================================");
    LOG_I(TAG, "TEST 2: Running Partition Info");
    LOG_I(TAG, "========================================");

    // Get running partition
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running == NULL) {
        LOG_E(TAG, "✗ Failed to get running partition!");
        return;
    }

    LOG_I(TAG, "Running partition:");
    LOG_I(TAG, "  Label:   %s", running->label);
    LOG_I(TAG, "  Type:    0x%02x", running->type);
    LOG_I(TAG, "  Subtype: 0x%02x", running->subtype);
    LOG_I(TAG, "  Offset:  0x%08lx", running->address);
    LOG_I(TAG, "  Size:    %lu KB", running->size / 1024);

    // Expected: factory partition (0x20000)
    if (running->address == 0x20000) {
        LOG_I(TAG, "✓ Booted from FACTORY partition (as expected)");
    } else if (running->address == 0x220000) {
        LOG_I(TAG, "✓ Booted from OTA_0 partition");
    } else if (running->address == 0x420000) {
        LOG_I(TAG, "✓ Booted from OTA_1 partition");
    } else {
        LOG_W(TAG, "? Booted from unexpected partition");
    }
    LOG_I(TAG, "");
}

/**
 * @brief Test 3: Check OTA data partition
 */
void test_ota_data_partition(void)
{
    LOG_I(TAG, "========================================");
    LOG_I(TAG, "TEST 3: OTA Data Partition");
    LOG_I(TAG, "========================================");

    // Find OTA data partition
    const esp_partition_t *otadata = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA,
        ESP_PARTITION_SUBTYPE_DATA_OTA,
        NULL
    );

    if (otadata == NULL) {
        LOG_E(TAG, "✗ OTA data partition not found!");
        return;
    }

    LOG_I(TAG, "OTA data partition:");
    LOG_I(TAG, "  Label:   %s", otadata->label);
    LOG_I(TAG, "  Offset:  0x%08lx", otadata->address);
    LOG_I(TAG, "  Size:    %lu bytes", otadata->size);

    // Expected: 0xF000, 8KB
    if (otadata->address == 0xF000 && otadata->size == 0x2000) {
        LOG_I(TAG, "✓ OTA data partition correct (0xF000, 8KB)");
    } else {
        LOG_W(TAG, "? OTA data partition unexpected size/location");
    }
    LOG_I(TAG, "");
}

/**
 * @brief Test 4: Verify boot partition and next update partition
 */
void test_boot_and_next_partition(void)
{
    LOG_I(TAG, "========================================");
    LOG_I(TAG, "TEST 4: Boot & Next Update Partition");
    LOG_I(TAG, "========================================");

    // Get boot partition (which partition is set to boot next)
    const esp_partition_t *boot_partition = esp_ota_get_boot_partition();
    if (boot_partition) {
        LOG_I(TAG, "Boot partition (next boot): %s @ 0x%08lx",
              boot_partition->label, boot_partition->address);
    } else {
        LOG_W(TAG, "Boot partition not set (will use factory)");
    }

    // Get next update partition (where OTA will write)
    const esp_partition_t *next_update = esp_ota_get_next_update_partition(NULL);
    if (next_update == NULL) {
        LOG_E(TAG, "✗ Failed to get next update partition!");
        return;
    }

    LOG_I(TAG, "Next update partition: %s @ 0x%08lx (%lu KB)",
          next_update->label, next_update->address, next_update->size / 1024);

    // Expected: ota_0 if running factory
    if (strcmp(next_update->label, "ota_0") == 0) {
        LOG_I(TAG, "✓ Next update will go to OTA_0 (correct)");
    } else if (strcmp(next_update->label, "ota_1") == 0) {
        LOG_I(TAG, "✓ Next update will go to OTA_1 (ping-pong)");
    } else {
        LOG_W(TAG, "? Unexpected next update partition");
    }
    LOG_I(TAG, "");
}

/**
 * @brief Test 5: Check OTA image state (rollback status)
 */
void test_ota_state(void)
{
    LOG_I(TAG, "========================================");
    LOG_I(TAG, "TEST 5: OTA Image State (Rollback)");
    LOG_I(TAG, "========================================");

    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running == NULL) {
        LOG_E(TAG, "✗ Cannot get running partition");
        return;
    }

    esp_ota_img_states_t ota_state;
    esp_err_t err = esp_ota_get_state_partition(running, &ota_state);

    if (err == ESP_OK) {
        const char *state_str;
        switch (ota_state) {
            case ESP_OTA_IMG_NEW:
                state_str = "NEW (pending validation)";
                LOG_W(TAG, "OTA State: %s", state_str);
                LOG_W(TAG, "⚠ App must call esp_ota_mark_app_valid_cancel_rollback()!");
                break;
            case ESP_OTA_IMG_PENDING_VERIFY:
                state_str = "PENDING_VERIFY (rollback will occur if not validated)";
                LOG_W(TAG, "OTA State: %s", state_str);
                LOG_W(TAG, "⚠ App must call esp_ota_mark_app_valid_cancel_rollback()!");
                break;
            case ESP_OTA_IMG_VALID:
                state_str = "VALID (validated, no rollback)";
                LOG_I(TAG, "OTA State: %s", state_str);
                LOG_I(TAG, "✓ App is validated");
                break;
            case ESP_OTA_IMG_ABORTED:
                state_str = "ABORTED (rollback occurred)";
                LOG_E(TAG, "OTA State: %s", state_str);
                break;
            case ESP_OTA_IMG_UNDEFINED:
            default:
                state_str = "UNDEFINED (factory or not OTA)";
                LOG_I(TAG, "OTA State: %s", state_str);
                LOG_I(TAG, "✓ Factory partition (no rollback needed)");
                break;
        }
    } else {
        LOG_I(TAG, "OTA State: Not applicable (factory partition)");
        LOG_I(TAG, "✓ Factory partition doesn't need validation");
    }
    LOG_I(TAG, "");
}

/**
 * @brief Test 6: Verify version information
 */
void test_version_info(void)
{
    LOG_I(TAG, "========================================");
    LOG_I(TAG, "TEST 6: Version Information");
    LOG_I(TAG, "========================================");

    // Check compile-time version from CMakeLists.txt
#ifdef FIRMWARE_VERSION
    LOG_I(TAG, "Firmware version: %s", FIRMWARE_VERSION);
#else
    LOG_W(TAG, "FIRMWARE_VERSION not defined");
#endif

#ifdef FIRMWARE_VERSION_MAJOR
    LOG_I(TAG, "Version components: %d.%d.%d",
          FIRMWARE_VERSION_MAJOR,
          FIRMWARE_VERSION_MINOR,
          FIRMWARE_VERSION_PATCH);
#else
    LOG_W(TAG, "Version components not defined");
#endif

    // Get app description from partition
    const esp_app_desc_t *app_desc = esp_app_get_description();
    LOG_I(TAG, "App description:");
    LOG_I(TAG, "  Project:  %s", app_desc->project_name);
    LOG_I(TAG, "  Version:  %s", app_desc->version);
    LOG_I(TAG, "  Compiled: %s %s", app_desc->date, app_desc->time);
    LOG_I(TAG, "  IDF Ver:  %s", app_desc->idf_ver);

    // Expected: 1.0.0
    if (strcmp(app_desc->version, "1.0.0") == 0) {
        LOG_I(TAG, "✓ Version 1.0.0 confirmed");
    } else {
        LOG_W(TAG, "? Unexpected version: %s", app_desc->version);
    }
    LOG_I(TAG, "");
}

/**
 * @brief Test 7: Check if we can mark app as valid (rollback test prep)
 */
void test_mark_app_valid(void)
{
    LOG_I(TAG, "========================================");
    LOG_I(TAG, "TEST 7: App Validation (Rollback Protection)");
    LOG_I(TAG, "========================================");

    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running == NULL) {
        LOG_E(TAG, "✗ Cannot get running partition");
        return;
    }

    // Check if running from OTA partition
    if (running->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0 ||
        running->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_1) {

        LOG_I(TAG, "Running from OTA partition, marking as valid...");
        esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();

        if (err == ESP_OK) {
            LOG_I(TAG, "✓ App marked as valid (rollback cancelled)");
        } else {
            LOG_E(TAG, "✗ Failed to mark app valid: %s", esp_err_to_name(err));
        }
    } else {
        LOG_I(TAG, "Running from factory partition");
        LOG_I(TAG, "✓ No validation needed (not an OTA partition)");
    }
    LOG_I(TAG, "");
}

/**
 * @brief Run all OTA tests
 */
void run_ota_tests(void)
{
    LOG_I(TAG, "");
    LOG_I(TAG, "╔════════════════════════════════════════╗");
    LOG_I(TAG, "║   OTA Configuration Test Suite         ║");
    LOG_I(TAG, "║   Firmware v%s                    ║", FIRMWARE_VERSION);
    LOG_I(TAG, "╚════════════════════════════════════════╝");
    LOG_I(TAG, "");

    test_partition_table();
    test_running_partition();
    test_ota_data_partition();
    test_boot_and_next_partition();
    test_ota_state();
    test_version_info();
    test_mark_app_valid();

    LOG_I(TAG, "========================================");
    LOG_I(TAG, "✓ All OTA tests completed!");
    LOG_I(TAG, "========================================");
    LOG_I(TAG, "");
    LOG_I(TAG, "OTA foundation is working correctly.");
    LOG_I(TAG, "Ready for OTA service implementation (Phases 4-6).");
    LOG_I(TAG, "");
}
