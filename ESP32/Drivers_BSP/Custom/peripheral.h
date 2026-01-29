/**
 * @file peripheral.h
 * @brief Peripheral identifiers and common definitions
 * 
 * Defines peripheral IDs used throughout the ESP32 Gateway project
 * for identifying and managing hardware peripherals.
 */

#ifndef PERIPHERAL_H
#define PERIPHERAL_H

#include <stdint.h>

/**
 * @brief Peripheral identifiers
 * 
 * Used to identify peripherals for initialization, status tracking,
 * and error reporting.
 */
typedef enum {
    PERIPHERAL_NONE = 0,        /**< No peripheral / invalid */
    
    // Communication peripherals
    PERIPHERAL_STM32_UART,      /**< UART for STM32 communication */
    PERIPHERAL_WIFI,            /**< WiFi interface */
    
    // Future peripherals (reserved)
    PERIPHERAL_BLUETOOTH,       /**< Bluetooth (reserved) */
    PERIPHERAL_ETHERNET,        /**< Ethernet (reserved) */
    
    PERIPHERAL_MAX              /**< Number of peripherals */
} peripheral_id_t;

/**
 * @brief Peripheral status
 */
typedef enum {
    PERIPHERAL_STATUS_UNKNOWN = 0,
    PERIPHERAL_STATUS_DISABLED,
    PERIPHERAL_STATUS_INITIALIZING,
    PERIPHERAL_STATUS_READY,
    PERIPHERAL_STATUS_ERROR,
    PERIPHERAL_STATUS_BUSY,
} peripheral_status_t;

/**
 * @brief Get peripheral name string
 * @param id Peripheral identifier
 * @return Peripheral name as string
 */
static inline const char* peripheral_get_name(peripheral_id_t id)
{
    switch (id) {
        case PERIPHERAL_STM32_UART: return "STM32_UART";
        case PERIPHERAL_WIFI:       return "WIFI";
        case PERIPHERAL_BLUETOOTH:  return "BLUETOOTH";
        case PERIPHERAL_ETHERNET:   return "ETHERNET";
        default:                    return "UNKNOWN";
    }
}

#endif // PERIPHERAL_H
