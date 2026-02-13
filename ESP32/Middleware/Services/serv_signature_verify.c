/**
 * @file serv_signature_verify.c
 * @brief Signature Verification Service Implementation
 */

#include "serv_signature_verify.h"
#include "stm32_public_key.h"
#include "esp_log.h"
#include "mbedtls/pk.h"
#include "mbedtls/md.h"
#include "mbedtls/error.h"
#include "mbedtls/base64.h"
#include <string.h>

static const char *TAG = "SIG_VERIFY";

// Internal state
static struct {
    mbedtls_pk_context stm32_pk_ctx;
    bool initialized;
} s_sig_verify = {
    .initialized = false,
};

const char* serv_signature_verify_status_str(sig_verify_status_t status)
{
    switch (status) {
        case SIG_VERIFY_OK:                   return "Success";
        case SIG_VERIFY_ERR_INVALID_ARG:      return "Invalid argument";
        case SIG_VERIFY_ERR_INVALID_SIGNATURE:return "Invalid signature";
        case SIG_VERIFY_ERR_INVALID_KEY:      return "Invalid key";
        case SIG_VERIFY_ERR_CRYPTO_FAILED:    return "Crypto operation failed";
        default:                              return "Unknown error";
    }
}

sig_verify_status_t serv_signature_verify_init(void)
{
    if (s_sig_verify.initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return SIG_VERIFY_OK;
    }

    ESP_LOGI(TAG, "Initializing signature verification service");

    // Initialize PK context
    mbedtls_pk_init(&s_sig_verify.stm32_pk_ctx);

    // Parse STM32 public key from embedded PEM
    int ret = mbedtls_pk_parse_public_key(&s_sig_verify.stm32_pk_ctx,
                                          (const unsigned char*)stm32_public_key_pem,
                                          strlen(stm32_public_key_pem) + 1);
    if (ret != 0) {
        char error_buf[100];
        mbedtls_strerror(ret, error_buf, sizeof(error_buf));
        ESP_LOGE(TAG, "Failed to parse STM32 public key: %s (0x%04X)", error_buf, -ret);
        mbedtls_pk_free(&s_sig_verify.stm32_pk_ctx);
        return SIG_VERIFY_ERR_INVALID_KEY;
    }

    // Verify key type
    mbedtls_pk_type_t key_type = mbedtls_pk_get_type(&s_sig_verify.stm32_pk_ctx);
    if (key_type != MBEDTLS_PK_ECKEY && key_type != MBEDTLS_PK_ECKEY_DH) {
        ESP_LOGE(TAG, "Invalid key type: %d (expected EC key for ED25519)", key_type);
        mbedtls_pk_free(&s_sig_verify.stm32_pk_ctx);
        return SIG_VERIFY_ERR_INVALID_KEY;
    }

    s_sig_verify.initialized = true;
    ESP_LOGI(TAG, "Signature verification service initialized");
    return SIG_VERIFY_OK;
}

sig_verify_status_t serv_signature_verify_stm32(const uint8_t *firmware_data,
                                                uint32_t firmware_size,
                                                const char *signature_b64)
{
    if (!s_sig_verify.initialized) {
        ESP_LOGE(TAG, "Service not initialized");
        return SIG_VERIFY_ERR_INVALID_ARG;
    }

    if (firmware_data == NULL || firmware_size == 0 || signature_b64 == NULL) {
        ESP_LOGE(TAG, "Invalid arguments");
        return SIG_VERIFY_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Verifying ED25519 signature for %lu bytes of firmware", firmware_size);

    // Decode base64 signature
    uint8_t signature[64];  // ED25519 signatures are always 64 bytes
    size_t signature_len = 0;

    int ret = mbedtls_base64_decode(signature, sizeof(signature), &signature_len,
                                    (const unsigned char*)signature_b64,
                                    strlen(signature_b64));
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to decode base64 signature: 0x%04X", -ret);
        return SIG_VERIFY_ERR_INVALID_SIGNATURE;
    }

    if (signature_len != 64) {
        ESP_LOGE(TAG, "Invalid signature length: %zu (expected 64)", signature_len);
        return SIG_VERIFY_ERR_INVALID_SIGNATURE;
    }

    // Compute SHA256 hash of firmware
    uint8_t hash[32];
    ret = mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
                     firmware_data, firmware_size, hash);
    if (ret != 0) {
        char error_buf[100];
        mbedtls_strerror(ret, error_buf, sizeof(error_buf));
        ESP_LOGE(TAG, "Failed to compute SHA256: %s (0x%04X)", error_buf, -ret);
        return SIG_VERIFY_ERR_CRYPTO_FAILED;
    }

    // Verify signature
    ret = mbedtls_pk_verify(&s_sig_verify.stm32_pk_ctx,
                           MBEDTLS_MD_SHA256,
                           hash, sizeof(hash),
                           signature, signature_len);

    if (ret != 0) {
        char error_buf[100];
        mbedtls_strerror(ret, error_buf, sizeof(error_buf));
        ESP_LOGE(TAG, "Signature verification failed: %s (0x%04X)", error_buf, -ret);
        return SIG_VERIFY_ERR_INVALID_SIGNATURE;
    }

    ESP_LOGI(TAG, "Signature verification successful");
    return SIG_VERIFY_OK;
}
