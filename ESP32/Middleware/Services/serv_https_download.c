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

static const char *TAG = "HTTPS_DOWNLOAD";

// Default configuration values
#define DEFAULT_TIMEOUT_MS   30000
#define DEFAULT_BUFFER_SIZE  4096
#define PROGRESS_REPORT_PCT  10  // Report every 10%

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

    // Allocate buffer for firmware
    buffer = (uint8_t*)malloc(config->expected_size);
    if (buffer == NULL) {
        LOG_E(TAG, "Failed to allocate %lu bytes", config->expected_size);
        result = HTTPS_DOWNLOAD_ERR_NO_MEM;
        goto cleanup;
    }

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

    // Fetch headers and verify content length
    int content_length = esp_http_client_fetch_headers(client);
    if (content_length < 0) {
        LOG_E(TAG, "Failed to fetch HTTP headers");
        result = HTTPS_DOWNLOAD_ERR_HTTP_ERROR;
        goto cleanup;
    }

    if ((uint32_t)content_length != config->expected_size) {
        LOG_W(TAG, "Content length mismatch: expected %lu, got %d",
                 config->expected_size, content_length);
    }

    // Check HTTP status code
    int status_code = esp_http_client_get_status_code(client);
    if (status_code != 200) {
        LOG_E(TAG, "HTTP error: status code %d", status_code);
        result = HTTPS_DOWNLOAD_ERR_HTTP_ERROR;
        goto cleanup;
    }

    // Download firmware
    while (ctx.bytes_downloaded < config->expected_size) {
        int read_len = esp_http_client_read(client,
                                            (char*)(buffer + ctx.bytes_downloaded),
                                            config->expected_size - ctx.bytes_downloaded);

        if (read_len < 0) {
            LOG_E(TAG, "HTTP read error");
            result = HTTPS_DOWNLOAD_ERR_HTTP_ERROR;
            goto cleanup;
        } else if (read_len == 0) {
            LOG_W(TAG, "Connection closed early, downloaded %lu/%lu bytes",
                     ctx.bytes_downloaded, config->expected_size);
            result = HTTPS_DOWNLOAD_ERR_SIZE_MISMATCH;
            goto cleanup;
        }

        ctx.bytes_downloaded += read_len;

        // Report progress
        int progress_pct = (ctx.bytes_downloaded * 100) / config->expected_size;
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
