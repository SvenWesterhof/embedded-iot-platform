/**
 * @file ota_test.h
 * @brief OTA Configuration Test Suite
 */

#ifndef OTA_TEST_H
#define OTA_TEST_H

/**
 * @brief Run all OTA configuration tests
 *
 * This function runs a comprehensive test suite to verify:
 * - Partition table accessibility
 * - Running partition information
 * - OTA data partition configuration
 * - Boot and next update partition logic
 * - OTA image state and rollback status
 * - Version information
 * - App validation API
 */
void run_ota_tests(void);

#endif // OTA_TEST_H
