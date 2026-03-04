/**
 * @file serv_firmware_update.c
 * @brief Firmware update service - dual-bank OTA receiver
 *
 * State machine: IDLE → ERASING → RECEIVING → VALIDATING → READY → (reset)
 */

#include "serv_firmware_update.h"
#include "hal_flash.h"
#include "portable_log.h"
#include <string.h>

static const char *TAG = "FW_UPDATE";

// ============================================================================
// Internal State
// ============================================================================

static struct {
    fw_update_state_t state;
    uint32_t total_size;
    uint32_t crc32_expected;
    uint16_t chunk_size;
    uint8_t version_major;
    uint8_t version_minor;
    uint8_t version_patch;
    uint32_t bytes_received;
    uint16_t chunks_received;
    uint16_t total_chunks_expected;
    uint8_t target_bank;
    uint32_t write_address;
    uint8_t error_code;
} s_fw = {
    .state = FW_UPDATE_IDLE,
};

// ============================================================================
// Public API
// ============================================================================

void serv_firmware_update_init(void)
{
    hal_flash_init();
    memset(&s_fw, 0, sizeof(s_fw));
    s_fw.state = FW_UPDATE_IDLE;

    LOG_I(TAG, "Firmware update service initialized (OTA staging: Bank 2)");
}

fw_update_svc_status_t serv_firmware_update_start(const cmd_fw_update_start_t *cmd)
{
    if (cmd == NULL) {
        return FW_UPDATE_SVC_ERR_STATE;
    }

    if (s_fw.state != FW_UPDATE_IDLE) {
        LOG_W(TAG, "Cannot start: state=%d (not IDLE)", s_fw.state);
        return FW_UPDATE_SVC_ERR_BUSY;
    }

    // Validate parameters
    if (cmd->total_size == 0 || cmd->total_size > HAL_FLASH_BANK_SIZE) {
        LOG_E(TAG, "Invalid firmware size: %u (max %u)",
              cmd->total_size, (uint32_t)HAL_FLASH_BANK_SIZE);
        return FW_UPDATE_SVC_ERR_SIZE;
    }

    if (cmd->chunk_size == 0 || cmd->chunk_size > PROTOCOL_MAX_PAYLOAD_SIZE) {
        LOG_E(TAG, "Invalid chunk size: %u", cmd->chunk_size);
        return FW_UPDATE_SVC_ERR_SIZE;
    }

    // Store update metadata
    s_fw.total_size = cmd->total_size;
    s_fw.crc32_expected = cmd->crc32;
    s_fw.chunk_size = cmd->chunk_size;
    s_fw.version_major = cmd->version_major;
    s_fw.version_minor = cmd->version_minor;
    s_fw.version_patch = cmd->version_patch;
    s_fw.bytes_received = 0;
    s_fw.chunks_received = 0;
    s_fw.total_chunks_expected = (uint16_t)((cmd->total_size + cmd->chunk_size - 1) / cmd->chunk_size);
    s_fw.error_code = 0;

    // Determine target bank (inactive bank)
    s_fw.target_bank = hal_flash_get_inactive_bank();
    s_fw.write_address = hal_flash_get_bank_base(s_fw.target_bank);

    LOG_I(TAG, "FW update start: v%u.%u.%u, %u bytes, %u chunks → bank %u",
          s_fw.version_major, s_fw.version_minor, s_fw.version_patch,
          s_fw.total_size, s_fw.total_chunks_expected, s_fw.target_bank);

    // Set state to ERASING — caller sends response, then calls erase separately
    s_fw.state = FW_UPDATE_ERASING;
    return FW_UPDATE_SVC_OK;
}

fw_update_svc_status_t serv_firmware_update_erase(void)
{
    if (s_fw.state != FW_UPDATE_ERASING) {
        LOG_W(TAG, "Cannot erase: state=%d (not ERASING)", s_fw.state);
        return FW_UPDATE_SVC_ERR_STATE;
    }

    LOG_I(TAG, "Erasing bank %u...", s_fw.target_bank);

    hal_flash_status_t fl_status = hal_flash_erase_bank(s_fw.target_bank);
    if (fl_status != HAL_FL_OK) {
        LOG_E(TAG, "Bank erase failed: %d", fl_status);
        s_fw.state = FW_UPDATE_ERROR;
        s_fw.error_code = (uint8_t)fl_status;
        return FW_UPDATE_SVC_ERR_FLASH;
    }

    LOG_I(TAG, "Bank %u erased, ready to receive chunks", s_fw.target_bank);
    s_fw.state = FW_UPDATE_RECEIVING;
    return FW_UPDATE_SVC_OK;
}

fw_update_svc_status_t serv_firmware_update_chunk(const cmd_fw_update_chunk_t *cmd,
                                                   uint16_t payload_len)
{
    if (cmd == NULL) {
        return FW_UPDATE_SVC_ERR_STATE;
    }

    if (s_fw.state != FW_UPDATE_RECEIVING) {
        LOG_W(TAG, "Cannot receive chunk: state=%d", s_fw.state);
        return FW_UPDATE_SVC_ERR_STATE;
    }

    // Verify sequential chunk order
    if (cmd->chunk_index != s_fw.chunks_received) {
        LOG_E(TAG, "Chunk sequence error: expected %u, got %u",
              s_fw.chunks_received, cmd->chunk_index);
        return FW_UPDATE_SVC_ERR_SEQUENCE;
    }

    // Verify chunk length
    uint16_t expected_data_len = payload_len - 4;  // Subtract chunk_index + chunk_length fields
    if (cmd->chunk_length != expected_data_len || cmd->chunk_length == 0) {
        LOG_E(TAG, "Chunk length mismatch: header=%u, payload=%u",
              cmd->chunk_length, expected_data_len);
        return FW_UPDATE_SVC_ERR_SIZE;
    }

    // Don't exceed total size
    if (s_fw.bytes_received + cmd->chunk_length > s_fw.total_size) {
        LOG_E(TAG, "Chunk would exceed total size (%u + %u > %u)",
              s_fw.bytes_received, cmd->chunk_length, s_fw.total_size);
        return FW_UPDATE_SVC_ERR_SIZE;
    }

    // Write chunk to flash
    hal_flash_status_t fl_status = hal_flash_write(
        s_fw.write_address, cmd->data, cmd->chunk_length);
    if (fl_status != HAL_FL_OK) {
        LOG_E(TAG, "Flash write failed at 0x%08X: %d",
              s_fw.write_address, fl_status);
        s_fw.state = FW_UPDATE_ERROR;
        s_fw.error_code = (uint8_t)fl_status;
        return FW_UPDATE_SVC_ERR_FLASH;
    }

    s_fw.write_address += cmd->chunk_length;
    s_fw.bytes_received += cmd->chunk_length;
    s_fw.chunks_received++;

    // Log progress at ~10% intervals
    if (s_fw.chunks_received % (s_fw.total_chunks_expected / 10 + 1) == 0 ||
        s_fw.chunks_received == s_fw.total_chunks_expected) {
        uint8_t pct = (uint8_t)((uint32_t)s_fw.chunks_received * 100 / s_fw.total_chunks_expected);
        LOG_I(TAG, "Progress: %u%% (%u/%u chunks, %u bytes)",
              pct, s_fw.chunks_received, s_fw.total_chunks_expected, s_fw.bytes_received);
    }

    return FW_UPDATE_SVC_OK;
}

fw_update_svc_status_t serv_firmware_update_end(const cmd_fw_update_end_t *cmd)
{
    if (cmd == NULL) {
        return FW_UPDATE_SVC_ERR_STATE;
    }

    if (s_fw.state != FW_UPDATE_RECEIVING) {
        LOG_W(TAG, "Cannot finalize: state=%d", s_fw.state);
        return FW_UPDATE_SVC_ERR_STATE;
    }

    // Verify all bytes received
    if (s_fw.bytes_received != s_fw.total_size) {
        LOG_E(TAG, "Size mismatch: received %u, expected %u",
              s_fw.bytes_received, s_fw.total_size);
        s_fw.state = FW_UPDATE_ERROR;
        return FW_UPDATE_SVC_ERR_SIZE;
    }

    // Validate CRC32 over written flash region
    s_fw.state = FW_UPDATE_VALIDATING;
    LOG_I(TAG, "Validating CRC32 over %u bytes at bank %u...",
          s_fw.total_size, s_fw.target_bank);

    uint32_t bank_base = hal_flash_get_bank_base(s_fw.target_bank);
    uint32_t computed_crc = hal_flash_compute_crc32(bank_base, s_fw.total_size);

    if (computed_crc != s_fw.crc32_expected) {
        LOG_E(TAG, "CRC32 mismatch: computed=0x%08X, expected=0x%08X",
              computed_crc, s_fw.crc32_expected);
        s_fw.state = FW_UPDATE_ERROR;
        s_fw.error_code = 0xFF;  // CRC error
        return FW_UPDATE_SVC_ERR_CRC;
    }

    LOG_I(TAG, "CRC32 valid (0x%08X)", computed_crc);
    s_fw.state = FW_UPDATE_READY;

    LOG_I(TAG, "Firmware v%u.%u.%u validated: %u bytes, CRC OK",
          s_fw.version_major, s_fw.version_minor, s_fw.version_patch,
          s_fw.total_size);

    LOG_I(TAG, "Firmware ready — waiting for apply or manual activation");
    return FW_UPDATE_SVC_OK;
}

void serv_firmware_update_apply(void)
{
    if (s_fw.state != FW_UPDATE_READY) {
        LOG_W(TAG, "Cannot apply: state=%d (not READY)", s_fw.state);
        return;
    }

    LOG_I(TAG, "Applying: setting update flag and resetting...");
    LOG_I(TAG, "New firmware: v%u.%u.%u, %u bytes",
          s_fw.version_major, s_fw.version_minor, s_fw.version_patch, s_fw.total_size);
    LOG_I(TAG, "Bootloader will copy from Bank 2 to app area on next boot");

    hal_flash_set_update_flag(s_fw.total_size, s_fw.crc32_expected,
                              s_fw.version_major, s_fw.version_minor, s_fw.version_patch);
    hal_flash_reset_for_update();
    // Does not return
}

fw_update_svc_status_t serv_firmware_update_abort(void)
{
    LOG_W(TAG, "Firmware update aborted (was in state %d)", s_fw.state);
    memset(&s_fw, 0, sizeof(s_fw));
    s_fw.state = FW_UPDATE_IDLE;
    return FW_UPDATE_SVC_OK;
}

void serv_firmware_update_get_status(resp_fw_update_status_t *status)
{
    if (status == NULL) {
        return;
    }

    status->state = (uint8_t)s_fw.state;
    status->bytes_received = s_fw.bytes_received;
    status->chunks_received = s_fw.chunks_received;
    status->error_code = s_fw.error_code;
}

