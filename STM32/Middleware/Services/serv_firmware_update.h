#ifndef SERV_FIRMWARE_UPDATE_H
#define SERV_FIRMWARE_UPDATE_H

/**
 * @file serv_firmware_update.h
 * @brief Firmware update service for dual-bank OTA
 *
 * Receives firmware chunks from ESP32 via protocol_handler,
 * writes to inactive flash bank, validates CRC32, and swaps boot bank.
 */

#include <stdint.h>
#include <stdbool.h>
#include "protocol_common.h"

// Service status codes
typedef enum {
    FW_UPDATE_SVC_OK = 0,
    FW_UPDATE_SVC_ERR_STATE,
    FW_UPDATE_SVC_ERR_FLASH,
    FW_UPDATE_SVC_ERR_CRC,
    FW_UPDATE_SVC_ERR_SIZE,
    FW_UPDATE_SVC_ERR_BUSY,
    FW_UPDATE_SVC_ERR_SEQUENCE,
} fw_update_svc_status_t;

/**
 * @brief Initialize firmware update service
 */
void serv_firmware_update_init(void);

/**
 * @brief Handle CMD_FW_UPDATE_START
 *
 * Validates parameters, stores metadata, sets state to ERASING.
 * Does NOT erase — call serv_firmware_update_erase() after sending the response.
 *
 * @param cmd Start command payload
 * @return FW_UPDATE_SVC_OK on success
 */
fw_update_svc_status_t serv_firmware_update_start(const cmd_fw_update_start_t *cmd);

/**
 * @brief Erase the inactive flash bank (blocking, ~7 seconds)
 *
 * Must be called after serv_firmware_update_start() and after the RESP_OK
 * has been sent to ESP32. Transitions state from ERASING → RECEIVING.
 *
 * @return FW_UPDATE_SVC_OK on success
 */
fw_update_svc_status_t serv_firmware_update_erase(void);

/**
 * @brief Handle CMD_FW_UPDATE_CHUNK
 *
 * Writes firmware chunk to flash at the next sequential address.
 *
 * @param cmd Chunk payload (includes index, length, and data)
 * @param payload_len Total payload length (4 + data bytes)
 * @return FW_UPDATE_SVC_OK on success
 */
fw_update_svc_status_t serv_firmware_update_chunk(const cmd_fw_update_chunk_t *cmd,
                                                   uint16_t payload_len);

/**
 * @brief Handle CMD_FW_UPDATE_END
 *
 * Validates CRC32 over written firmware. On success, state becomes READY.
 *
 * @param cmd End command payload
 * @return FW_UPDATE_SVC_OK on success
 */
fw_update_svc_status_t serv_firmware_update_end(const cmd_fw_update_end_t *cmd);

/**
 * @brief Swap boot bank and reset (does not return)
 *
 * Must be called after serv_firmware_update_end() succeeds and after
 * the response has been sent to ESP32. Allows UART TX to complete first.
 */
void serv_firmware_update_apply(void);

/**
 * @brief Handle CMD_FW_UPDATE_ABORT
 *
 * Cancels any in-progress update and resets state to IDLE.
 *
 * @return FW_UPDATE_SVC_OK always
 */
fw_update_svc_status_t serv_firmware_update_abort(void);

/**
 * @brief Handle CMD_FW_UPDATE_STATUS
 *
 * Fills the status response with current update state.
 *
 * @param status Output: status response payload
 */
void serv_firmware_update_get_status(resp_fw_update_status_t *status);

#endif // SERV_FIRMWARE_UPDATE_H
