/**
 * @file serv_https_download.c
 * @brief HTTPS Download Service Implementation
 */

#include "serv_https_download.h"
#include "portable_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"

static const char *TAG = "HTTPS_DOWNLOAD";

// Default configuration values
#define DEFAULT_TIMEOUT_MS      60000  // 60s per recv() — S3 can pause between TCP segments
#define DEFAULT_BUFFER_SIZE     4096
#define PROGRESS_REPORT_PCT     10     // Report every 10%
#define READ_CHUNK_SIZE         4096   // Max bytes per esp_http_client_read() call
#define MAX_EAGAIN_RETRIES      5      // LWIP returns 0+EAGAIN on SO_RCVTIMEO; retry before failing

// Internal context for HTTP event handler
typedef struct {
    https_download_progress_cb_t progress_cb;
    void *user_data;
    uint32_t bytes_downloaded;
    uint32_t total_bytes;
    int last_progress_pct;
} download_context_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    download_context_t *ctx = (download_context_t*)evt->user_data;

    switch (evt->event_id) {
        case HTTP_EVENT_ERROR:
            LOG_E(TAG, "HTTP error");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            LOG_I(TAG, "HTTP connected");
            break;
        case HTTP_EVENT_HEADER_SENT:
            LOG_D(TAG, "HTTP headers sent");
            break;
        case HTTP_EVENT_ON_HEADER:
            LOG_D(TAG, "HTTP header: %s: %s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            // Data handling is done in esp_http_client_read()
            break;
        case HTTP_EVENT_ON_FINISH:
            LOG_I(TAG, "HTTP download finished");
            break;
        case HTTP_EVENT_DISCONNECTED:
            LOG_I(TAG, "HTTP disconnected");
            break;
        default:
            break;
    }
    return ESP_OK;
}

const char* serv_https_download_status_str(https_download_status_t status)
{
    switch (status) {
        case HTTPS_DOWNLOAD_OK:                return "Success";
        case HTTPS_DOWNLOAD_ERR_INVALID_ARG:   return "Invalid argument";
        case HTTPS_DOWNLOAD_ERR_NO_MEM:        return "Out of memory";
        case HTTPS_DOWNLOAD_ERR_CONNECT_FAILED:return "Connection failed";
        case HTTPS_DOWNLOAD_ERR_HTTP_ERROR:    return "HTTP error";
        case HTTPS_DOWNLOAD_ERR_SIZE_MISMATCH: return "Size mismatch";
        case HTTPS_DOWNLOAD_ERR_TIMEOUT:       return "Timeout";
        default:                               return "Unknown error";
    }
}

https_download_status_t serv_https_download_stream(const https_download_config_t *config,
                                                    https_chunk_cb_t chunk_cb,
                                                    void *user_data)
{
    if (config == NULL || config->url == NULL || chunk_cb == NULL) {
        LOG_E(TAG, "Invalid arguments");
        return HTTPS_DOWNLOAD_ERR_INVALID_ARG;
    }

    if (config->expected_size == 0) {
        LOG_E(TAG, "Expected size must be specified");
        return HTTPS_DOWNLOAD_ERR_INVALID_ARG;
    }

    LOG_I(TAG, "Starting streaming download: %u bytes", config->expected_size);

    https_download_status_t result = HTTPS_DOWNLOAD_OK;
    esp_http_client_handle_t client = NULL;

    // 4KB working buffer — only this much is ever in RAM at once
    uint8_t *chunk_buf = (uint8_t*)malloc(READ_CHUNK_SIZE);
    if (chunk_buf == NULL) {
        LOG_E(TAG, "Failed to allocate %u byte chunk buffer", READ_CHUNK_SIZE);
        return HTTPS_DOWNLOAD_ERR_NO_MEM;
    }

    uint32_t bytes_downloaded = 0;
    int      last_progress_pct = 0;

    esp_http_client_config_t http_config = {
        .url                        = config->url,
        .event_handler              = http_event_handler,
        .user_data                  = NULL,
        .timeout_ms                 = config->timeout_ms > 0 ? config->timeout_ms : DEFAULT_TIMEOUT_MS,
        .buffer_size                = config->buffer_size > 0 ? config->buffer_size : DEFAULT_BUFFER_SIZE,
        .skip_cert_common_name_check = true,
        .crt_bundle_attach          = esp_crt_bundle_attach,
    };

    client = esp_http_client_init(&http_config);
    if (client == NULL) {
        LOG_E(TAG, "Failed to initialize HTTP client");
        result = HTTPS_DOWNLOAD_ERR_CONNECT_FAILED;
        goto cleanup;
    }

    if (esp_http_client_open(client, 0) != ESP_OK) {
        LOG_E(TAG, "Failed to open HTTP connection");
        result = HTTPS_DOWNLOAD_ERR_CONNECT_FAILED;
        goto cleanup;
    }

    int content_length = esp_http_client_fetch_headers(client);
    int status_code    = esp_http_client_get_status_code(client);

    LOG_I(TAG, "HTTP response: status=%d content_length=%d", status_code, content_length);

    if (content_length < 0 && status_code == 0) {
        LOG_E(TAG, "Failed to fetch HTTP headers (no response from server)");
        result = HTTPS_DOWNLOAD_ERR_HTTP_ERROR;
        goto cleanup;
    }

    if (status_code != 200) {
        LOG_E(TAG, "HTTP error: status code %d", status_code);
        result = HTTPS_DOWNLOAD_ERR_HTTP_ERROR;
        goto cleanup;
    }

    if (content_length < 0) {
        LOG_W(TAG, "No Content-Length in response, using expected size %u", config->expected_size);
    } else if ((uint32_t)content_length != config->expected_size) {
        LOG_W(TAG, "Content length mismatch: expected %u, got %d",
                 config->expected_size, content_length);
    }

    // Stream: read a chunk, deliver to callback, repeat
    int eagain_retries = 0;
    while (bytes_downloaded < config->expected_size) {
        uint32_t remaining = config->expected_size - bytes_downloaded;
        uint32_t to_read   = remaining < READ_CHUNK_SIZE ? remaining : READ_CHUNK_SIZE;

        int read_len = esp_http_client_read(client, (char*)chunk_buf, (int)to_read);

        if (read_len < 0) {
            if (errno == EAGAIN && eagain_retries < MAX_EAGAIN_RETRIES) {
                eagain_retries++;
                LOG_W(TAG, "Receive timeout (EAGAIN), retry %d/%d — %u/%u bytes",
                         eagain_retries, MAX_EAGAIN_RETRIES,
                         bytes_downloaded, config->expected_size);
                vTaskDelay(pdMS_TO_TICKS(200));
                continue;
            }
            LOG_E(TAG, "HTTP read error after %u/%u bytes (errno=%d)",
                     bytes_downloaded, config->expected_size, errno);
            result = HTTPS_DOWNLOAD_ERR_HTTP_ERROR;
            goto cleanup;
        } else if (read_len == 0) {
            LOG_W(TAG, "Connection closed early at %u/%u bytes",
                     bytes_downloaded, config->expected_size);
            result = HTTPS_DOWNLOAD_ERR_SIZE_MISMATCH;
            goto cleanup;
        }

        eagain_retries = 0;

        if (!chunk_cb(chunk_buf, (uint32_t)read_len, user_data)) {
            LOG_E(TAG, "Chunk callback aborted at %u/%u bytes",
                     bytes_downloaded, config->expected_size);
            result = HTTPS_DOWNLOAD_ERR_HTTP_ERROR;
            goto cleanup;
        }

        bytes_downloaded += read_len;

        int progress_pct = (int)((bytes_downloaded * 100) / config->expected_size);
        if (progress_pct >= last_progress_pct + PROGRESS_REPORT_PCT) {
            LOG_I(TAG, "Download progress: %d%% (%u/%u bytes)",
                     progress_pct, bytes_downloaded, config->expected_size);
            last_progress_pct = progress_pct;
            if (config->progress_cb != NULL) {
                config->progress_cb(bytes_downloaded, config->expected_size, config->user_data);
            }
        }
    }

    if (bytes_downloaded != config->expected_size) {
        LOG_E(TAG, "Incomplete download: %u/%u bytes", bytes_downloaded, config->expected_size);
        result = HTTPS_DOWNLOAD_ERR_SIZE_MISMATCH;
        goto cleanup;
    }

    LOG_I(TAG, "Streaming download complete: %u bytes", bytes_downloaded);

cleanup:
    if (client != NULL)    esp_http_client_cleanup(client);
    if (chunk_buf != NULL) free(chunk_buf);
    return result;
}

