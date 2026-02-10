/**
 * @file crc16.h
 * @brief CRC16-CCITT implementation for packet validation
 *
 * Shared between ESP32 and STM32 for UART packet framing.
 * Uses polynomial 0x1021 with initial value 0xFFFF.
 */

#ifndef CRC16_H
#define CRC16_H

#include <stdint.h>
#include <stddef.h>

/**
 * @brief Calculate CRC16-CCITT checksum
 *
 * @param data Pointer to data buffer
 * @param length Number of bytes to process
 * @return CRC16 checksum value
 */
uint16_t crc16_ccitt(const uint8_t *data, size_t length);

#endif // CRC16_H
