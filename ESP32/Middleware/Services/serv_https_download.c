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

https_download_status_t serv_https_download(const https_download_config_t *config,
                                            uint8_t **out_buffer,
                                            uint32_t *out_size)
{
    if (config == NULL || config->url == NULL || out_buffer == NULL || out_size == NULL) {
        LOG_E(TAG, "Invalid arguments");
        return HTTPS_DOWNLOAD_ERR_INVALID_ARG;
    }

    if (config->expected_size == 0) {
        LOG_E(TAG, "Expected size must be specified");
        return HTTPS_DOWNLOAD_ERR_INVALID_ARG;
    }

    LOG_I(TAG, "Starting download: %s", config->url);
    LOG_I(TAG, "Expected size: %lu bytes", config->expected_size);

    https_download_status_t result = HTTPS_DOWNLOAD_OK;
    uint8_t *buffer = NULL;
    esp_http_client_handle_t client = NULL;

    // Initialize download context
    download_context_t ctx = {
        .progress_cb = config->progress_cb,
        .user_data = config->user_data,
        .bytes_downloaded = 0,
        .total_bytes = config->expected_size,
        .last_progress_pct = 0,
    };

    // NOTE: firmware buffer is allocated AFTER headers are confirmed (see below).
    // Allocating 100KB+ up front before TLS handshake exhausts the heap, which
    // prevents LWIP from allocating pbufs for incoming TCP data (TCP window → 0).

    // Configure HTTP client
    esp_http_client_config_t http_config = {
        .url = config->url,
        .event_handler = http_event_handler,
        .user_data = &ctx,
        .timeout_ms = config->timeout_ms > 0 ? config->timeout_ms : DEFAULT_TIMEOUT_MS,
        .buffer_size = config->buffer_size > 0 ? config->buffer_size : DEFAULT_BUFFER_SIZE,

        // TLS/SSL Configuration for HTTPS
        // NOTE: For production, replace with proper certificate verification
        // Either use .cert_pem with AWS root CA certificate or enable global CA store
        .skip_cert_common_name_check = true,  // Skip hostname verification
        .crt_bundle_attach = esp_crt_bundle_attach,  // Use ESP32 certificate bundle
    };

    client = esp_http_client_init(&http_config);
    if (client == NULL) {
        LOG_E(TAG, "Failed to initialize HTTP client");
        result = HTTPS_DOWNLOAD_ERR_CONNECT_FAILED;
        goto cleanup;
    }

    // Open connection
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        LOG_E(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        result = HTTPS_DOWNLOAD_ERR_CONNECT_FAILED;
        goto cleanup;
    }

    // Fetch headers — returns Content-Length or -1 (transport error OR chunked response)
    int content_length = esp_http_client_fetch_headers(client);

    // Always read status code first so we can log it for both success and failure paths.
    // NOTE: must be called after fetch_headers(); returns 0 if no response was received.
    int status_code = esp_http_client_get_status_code(client);

    LOG_I(TAG, "HTTP response: status=%d content_length=%d", status_code, content_length);

    if (content_length < 0 && status_code == 0) {
        // Transport-level failure — server never sent a response (connection dropped,
        // TLS error, or recv timeout during header read).
        LOG_E(TAG, "Failed to fetch HTTP headers (no response from server)");
        result = HTTPS_DOWNLOAD_ERR_HTTP_ERROR;
        goto cleanup;
    }

    if (status_code != 200) {
        // Server responded with an error (403 expired URL, 404 not found, etc.)
        LOG_E(TAG, "HTTP error: status code %d", status_code);
        result = HTTPS_DOWNLOAD_ERR_HTTP_ERROR;
        goto cleanup;
    }

    if (content_length < 0) {
        // status 200 but no Content-Length (S3 chunked transfer encoding).
        // Proceed using expected_size from the OTA manifest.
        LOG_W(TAG, "No Content-Length in response, using expected size %lu",
                 config->expected_size);
    } else if ((uint32_t)content_length != config->expected_size) {
        LOG_W(TAG, "Content length mismatch: expected %lu, got %d",
                 config->expected_size, content_length);
    }

    // Allocate firmware buffer NOW — after TLS handshake and header exchange are done.
    // Allocating before the connection means ~100KB is taken from the heap while LWIP
    // still needs to allocate pbufs for the TLS records and TCP receive window, which
    // causes TCP zero-window → S3 stops sending body data → recv() times out.
    LOG_I(TAG, "Free heap before firmware buffer alloc: %lu bytes",
             (uint32_t)esp_get_free_heap_size());
    buffer = (uint8_t*)malloc(config->expected_size);
    if (buffer == NULL) {
        LOG_E(TAG, "Failed to allocate %lu bytes (free heap: %lu)",
                 config->expected_size, (uint32_t)esp_get_free_heap_size());
        result = HTTPS_DOWNLOAD_ERR_NO_MEM;
        goto cleanup;
    }
    LOG_I(TAG, "Firmware buffer allocated, free heap now: %lu bytes",
             (uint32_t)esp_get_free_heap_size());

    // Download firmware in READ_CHUNK_SIZE chunks.
    // Reading the full expected_size at once causes esp_http_client_read() to loop
    // internally calling recv() until all bytes arrive. A single 30s SO_RCVTIMEO
    // expiry (LWIP returns 0+EAGAIN) then aborts the whole download.
    // Chunked reads keep each recv() short and allow EAGAIN retries per chunk.
    int eagain_retries = 0;
    while (ctx.bytes_downloaded < config->expected_size) {
        uint32_t remaining = config->expected_size - ctx.bytes_downloaded;
        uint32_t to_read   = remaining < READ_CHUNK_SIZE ? remaining : READ_CHUNK_SIZE;

        int read_len = esp_http_client_read(client,
                                            (char*)(buffer + ctx.bytes_downloaded),
                                            (int)to_read);

        if (read_len < 0) {
            // LWIP: recv() timeout fires → transport returns 0+EAGAIN →
            // esp_http_client_read returns -1, errno is still EAGAIN (11).
            if (errno == EAGAIN && eagain_retries < MAX_EAGAIN_RETRIES) {
                eagain_retries++;
                LOG_W(TAG, "Receive timeout (EAGAIN), retry %d/%d — %lu/%lu bytes so far",
                         eagain_retries, MAX_EAGAIN_RETRIES,
                         ctx.bytes_downloaded, config->expected_size);
                vTaskDelay(pdMS_TO_TICKS(200));
                continue;
            }
            LOG_E(TAG, "HTTP read error after %lu/%lu bytes (errno=%d)",
                     ctx.bytes_downloaded, config->expected_size, errno);
            result = HTTPS_DOWNLOAD_ERR_HTTP_ERROR;
            goto cleanup;
        } else if (read_len == 0) {
            LOG_W(TAG, "Connection closed early, downloaded %lu/%lu bytes",
                     ctx.bytes_downloaded, config->expected_size);
            result = HTTPS_DOWNLOAD_ERR_SIZE_MISMATCH;
            goto cleanup;
        }

        eagain_retries = 0;  // reset on successful read
        ctx.bytes_downloaded += read_len;

        // Report progress
        int progress_pct = (int)((ctx.bytes_downloaded * 100) / config->expected_size);
        if (progress_pct >= ctx.last_progress_pct + PROGRESS_REPORT_PCT) {
            LOG_I(TAG, "Download progress: %d%% (%lu/%lu bytes)",
                     progress_pct, ctx.bytes_downloaded, config->expected_size);
            ctx.last_progress_pct = progress_pct;

            // Call user callback if provided
            if (ctx.progress_cb != NULL) {
                ctx.progress_cb(ctx.bytes_downloaded, config->expected_size, ctx.user_data);
            }
        }
    }

    // Verify final size
    if (ctx.bytes_downloaded != config->expected_size) {
        LOG_E(TAG, "Incomplete download: %lu/%lu bytes",
                 ctx.bytes_downloaded, config->expected_size);
        result = HTTPS_DOWNLOAD_ERR_SIZE_MISMATCH;
        goto cleanup;
    }

    LOG_I(TAG, "Download complete: %lu bytes", ctx.bytes_downloaded);

    // Success - return buffer to caller
    *out_buffer = buffer;
    *out_size = ctx.bytes_downloaded;
    buffer = NULL;  // Prevent cleanup from freeing it

cleanup:
    if (client != NULL) {
        esp_http_client_cleanup(client);
    }

    if (buffer != NULL) {
        free(buffer);
    }

    return result;
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

    LOG_I(TAG, "Starting streaming download: %lu bytes", config->expected_size);

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
        LOG_W(TAG, "No Content-Length in response, using expected size %lu", config->expected_size);
    } else if ((uint32_t)content_length != config->expected_size) {
        LOG_W(TAG, "Content length mismatch: expected %lu, got %d",
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
                LOG_W(TAG, "Receive timeout (EAGAIN), retry %d/%d — %lu/%lu bytes",
                         eagain_retries, MAX_EAGAIN_RETRIES,
                         bytes_downloaded, config->expected_size);
                vTaskDelay(pdMS_TO_TICKS(200));
                continue;
            }
            LOG_E(TAG, "HTTP read error after %lu/%lu bytes (errno=%d)",
                     bytes_downloaded, config->expected_size, errno);
            result = HTTPS_DOWNLOAD_ERR_HTTP_ERROR;
            goto cleanup;
        } else if (read_len == 0) {
            LOG_W(TAG, "Connection closed early at %lu/%lu bytes",
                     bytes_downloaded, config->expected_size);
            result = HTTPS_DOWNLOAD_ERR_SIZE_MISMATCH;
            goto cleanup;
        }

        eagain_retries = 0;

        if (!chunk_cb(chunk_buf, (uint32_t)read_len, user_data)) {
            LOG_E(TAG, "Chunk callback aborted at %lu/%lu bytes",
                     bytes_downloaded, config->expected_size);
            result = HTTPS_DOWNLOAD_ERR_HTTP_ERROR;
            goto cleanup;
        }

        bytes_downloaded += read_len;

        int progress_pct = (int)((bytes_downloaded * 100) / config->expected_size);
        if (progress_pct >= last_progress_pct + PROGRESS_REPORT_PCT) {
            LOG_I(TAG, "Download progress: %d%% (%lu/%lu bytes)",
                     progress_pct, bytes_downloaded, config->expected_size);
            last_progress_pct = progress_pct;
            if (config->progress_cb != NULL) {
                config->progress_cb(bytes_downloaded, config->expected_size, config->user_data);
            }
        }
    }

    if (bytes_downloaded != config->expected_size) {
        LOG_E(TAG, "Incomplete download: %lu/%lu bytes", bytes_downloaded, config->expected_size);
        result = HTTPS_DOWNLOAD_ERR_SIZE_MISMATCH;
        goto cleanup;
    }

    LOG_I(TAG, "Streaming download complete: %lu bytes", bytes_downloaded);

cleanup:
    if (client != NULL)    esp_http_client_cleanup(client);
    if (chunk_buf != NULL) free(chunk_buf);
    return result;
}
