#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/**
 * @file config.h
 * @brief Application-level configuration defines
 * 
 * This file contains application-wide configuration constants,
 * feature flags, and system parameters.
 */

// System Configuration
#define APP_VERSION_MAJOR       1
#define APP_VERSION_MINOR       0
#define APP_VERSION_PATCH       0

// Feature Flags
#define FEATURE_BLINKY_ENABLED          1
#define FEATURE_TEMPERATURE_ENABLED     1

// Timing Configuration (in milliseconds)
#define APP_MAIN_LOOP_PERIOD_MS         100
#define APP_HEARTBEAT_PERIOD_MS         1000

// Debug Configuration
#define APP_DEBUG_ENABLED               1

#if APP_DEBUG_ENABLED
    #define APP_LOG(msg)    // TODO: Add logging implementation
#else
    #define APP_LOG(msg)
#endif

#endif // APP_CONFIG_H
