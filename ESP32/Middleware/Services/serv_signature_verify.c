/**
 * @file serv_signature_verify.c
 * @brief Signature Verification Service Implementation - RSA-2048
 */

#include "serv_signature_verify.h"
#include "stm32_public_key.h"
#include "esp_log.h"
#include "mbedtls/pk.h"
#include "mbedtls/md.h"
#include "mbedtls/error.h"
#include "mbedtls/base64.h"
#include "mbedtls/rsa.h"
#include <string.h>

static const char *TAG = "SIG_VERIFY";

// Internal state
static struct {
    mbedtls_pk_context pk_ctx;  // RSA public key context
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

    ESP_LOGI(TAG, "Initializing RSA signature verification service");

    // Initialize PK context
    mbedtls_pk_init(&s_sig_verify.pk_ctx);

    // Parse RSA public key from embedded PEM
    int ret = mbedtls_pk_parse_public_key(&s_sig_verify.pk_ctx,
                                          (const unsigned char*)stm32_public_key_pem,
                                          strlen(stm32_public_key_pem) + 1);
    if (ret != 0) {
        char error_buf[100];
        mbedtls_strerror(ret, error_buf, sizeof(error_buf));
        ESP_LOGE(TAG, "Failed to parse RSA public key: %s (0x%04X)", error_buf, -ret);
        mbedtls_pk_free(&s_sig_verify.pk_ctx);
        return SIG_VERIFY_ERR_INVALID_KEY;
    }

    // Verify key type is RSA
    mbedtls_pk_type_t key_type = mbedtls_pk_get_type(&s_sig_verify.pk_ctx);
    if (key_type != MBEDTLS_PK_RSA) {
        ESP_LOGE(TAG, "Invalid key type: %d (expected RSA)", key_type);
        mbedtls_pk_free(&s_sig_verify.pk_ctx);
        return SIG_VERIFY_ERR_INVALID_KEY;
    }

    // Get and log key size
    size_t key_bits = mbedtls_pk_get_bitlen(&s_sig_verify.pk_ctx);
    ESP_LOGI(TAG, "RSA public key loaded: %u bits", (unsigned int)key_bits);

    s_sig_verify.initialized = true;
    ESP_LOGI(TAG, "Signature verification service initialized successfully");
    return SIG_VERIFY_OK;
}

sig_verify_status_t serv_signature_verify_firmware(const uint8_t *firmware_data,
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

    ESP_LOGI(TAG, "Verifying RSA signature for %lu bytes of firmware", firmware_size);

    // Decode base64 signature
    // RSA-2048 produces 256 byte signatures, RSA-3072 produces 384 bytes
    uint8_t signature[512];  // Buffer large enough for RSA-3072
    size_t signature_len = 0;

    int ret = mbedtls_base64_decode(signature, sizeof(signature), &signature_len,
                                    (const unsigned char*)signature_b64,
                                    strlen(signature_b64));
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to decode base64 signature: 0x%04X", -ret);
        return SIG_VERIFY_ERR_INVALID_SIGNATURE;
    }

    ESP_LOGI(TAG, "Decoded signature: %zu bytes", signature_len);

    // Validate signature length (should be 256 for RSA-2048 or 384 for RSA-3072)
    if (signature_len != 256 && signature_len != 384) {
        ESP_LOGE(TAG, "Invalid signature length: %zu (expected 256 or 384)", signature_len);
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

    // Verify RSA signature using PKCS#1 v1.5 padding with SHA256
    ret = mbedtls_pk_verify(&s_sig_verify.pk_ctx,
                           MBEDTLS_MD_SHA256,
                           hash, sizeof(hash),
                           signature, signature_len);

    if (ret != 0) {
        char error_buf[100];
        mbedtls_strerror(ret, error_buf, sizeof(error_buf));
        ESP_LOGE(TAG, "RSA signature verification failed: %s (0x%04X)", error_buf, -ret);
        return SIG_VERIFY_ERR_INVALID_SIGNATURE;
    }

    ESP_LOGI(TAG, "✓ RSA signature verification successful");
    return SIG_VERIFY_OK;
}

sig_verify_status_t serv_signature_verify_hash(const uint8_t *sha256_hash,
                                                const char *signature_b64)
{
    if (!s_sig_verify.initialized) {
        ESP_LOGE(TAG, "Service not initialized");
        return SIG_VERIFY_ERR_INVALID_ARG;
    }

    if (sha256_hash == NULL || signature_b64 == NULL) {
        ESP_LOGE(TAG, "Invalid arguments");
        return SIG_VERIFY_ERR_INVALID_ARG;
    }

    // Decode base64 signature
    uint8_t signature[512];
    size_t signature_len = 0;

    int ret = mbedtls_base64_decode(signature, sizeof(signature), &signature_len,
                                    (const unsigned char*)signature_b64,
                                    strlen(signature_b64));
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to decode base64 signature: 0x%04X", -ret);
        return SIG_VERIFY_ERR_INVALID_SIGNATURE;
    }

    if (signature_len != 256 && signature_len != 384) {
        ESP_LOGE(TAG, "Invalid signature length: %zu", signature_len);
        return SIG_VERIFY_ERR_INVALID_SIGNATURE;
    }

    // Verify RSA signature over the provided SHA256 digest
    ret = mbedtls_pk_verify(&s_sig_verify.pk_ctx,
                            MBEDTLS_MD_SHA256,
                            sha256_hash, 32,
                            signature, signature_len);
    if (ret != 0) {
        char error_buf[100];
        mbedtls_strerror(ret, error_buf, sizeof(error_buf));
        ESP_LOGE(TAG, "RSA signature verification failed: %s (0x%04X)", error_buf, -ret);
        return SIG_VERIFY_ERR_INVALID_SIGNATURE;
    }

    ESP_LOGI(TAG, "✓ RSA signature verification successful");
    return SIG_VERIFY_OK;
}
