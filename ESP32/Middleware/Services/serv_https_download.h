/**
 * @file serv_https_download.h
 * @brief HTTPS Download Service - Reusable firmware download over HTTPS
 *
 * Shared service for downloading firmware files from S3 or other HTTPS sources.
 * Used by both ESP32 and STM32 OTA managers.
 */

#ifndef SERV_HTTPS_DOWNLOAD_H
#define SERV_HTTPS_DOWNLOAD_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// Download Service Status Codes
typedef enum {
    HTTPS_DOWNLOAD_OK = 0,
    HTTPS_DOWNLOAD_ERR_INVALID_ARG,
    HTTPS_DOWNLOAD_ERR_NO_MEM,
    HTTPS_DOWNLOAD_ERR_CONNECT_FAILED,
    HTTPS_DOWNLOAD_ERR_HTTP_ERROR,
    HTTPS_DOWNLOAD_ERR_SIZE_MISMATCH,
    HTTPS_DOWNLOAD_ERR_TIMEOUT,
} https_download_status_t;

// Progress callback function type
typedef void (*https_download_progress_cb_t)(uint32_t bytes_downloaded, uint32_t total_bytes, void *user_data);

// Download Configuration
typedef struct {
    const char *url;                          // HTTPS URL
    uint32_t expected_size;                   // Expected size in bytes (0 = unknown)
    uint32_t timeout_ms;                      // Timeout per operation (default: 30000ms)
    uint32_t buffer_size;                     // HTTP buffer size (default: 4096)
    https_download_progress_cb_t progress_cb; // Optional progress callback
    void *user_data;                          // User data for progress callback
} https_download_config_t;

/**
 * @brief Download file from HTTPS URL into memory
 *
 * Allocates buffer internally and returns pointer. Caller must free() the buffer.
 *
 * @param config Download configuration
 * @param out_buffer Pointer to receive allocated buffer (caller must free)
 * @param out_size Pointer to receive actual downloaded size
 * @return HTTPS_DOWNLOAD_OK on success, error code otherwise
 */
https_download_status_t serv_https_download(const https_download_config_t *config,
                                            uint8_t **out_buffer,
                                            uint32_t *out_size);

/**
 * @brief Get status code as human-readable string
 *
 * @param status Status code
 * @return String description
 */
const char* serv_https_download_status_str(https_download_status_t status);

#endif // SERV_HTTPS_DOWNLOAD_H
