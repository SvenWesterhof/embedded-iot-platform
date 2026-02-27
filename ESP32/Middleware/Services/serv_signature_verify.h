/**
 * @file serv_signature_verify.h
 * @brief Signature Verification Service - RSA signature verification for firmware
 *
 * Verifies RSA-2048 signatures for STM32 and ESP32 firmware downloads.
 * Uses embedded public key and mbedTLS for cryptographic operations.
 */

#ifndef SERV_SIGNATURE_VERIFY_H
#define SERV_SIGNATURE_VERIFY_H

#include <stdint.h>
#include <stdbool.h>

// Signature Verification Status Codes
typedef enum {
    SIG_VERIFY_OK = 0,
    SIG_VERIFY_ERR_INVALID_ARG,
    SIG_VERIFY_ERR_INVALID_SIGNATURE,
    SIG_VERIFY_ERR_INVALID_KEY,
    SIG_VERIFY_ERR_CRYPTO_FAILED,
} sig_verify_status_t;

// Signature algorithm types
typedef enum {
    SIG_ALG_RSA_2048,
    SIG_ALG_RSA_3072,  // Future expansion for higher security
} sig_algorithm_t;

/**
 * @brief Initialize signature verification service
 *
 * Loads embedded public keys and initializes crypto library
 *
 * @return SIG_VERIFY_OK on success
 */
sig_verify_status_t serv_signature_verify_init(void);

/**
 * @brief Verify RSA-2048 signature against a pre-computed SHA256 hash
 *
 * Use when SHA256 was computed incrementally during a streaming download,
 * rather than from a full in-memory buffer.
 * RSA PKCS#1 v1.5 signs a hash of the data, so verification only needs the
 * 32-byte digest — not the full firmware binary.
 *
 * @param sha256_hash 32-byte raw SHA256 digest of the firmware
 * @param signature_b64 Base64-encoded RSA-2048 signature
 * @return SIG_VERIFY_OK if signature is valid
 */
sig_verify_status_t serv_signature_verify_hash(const uint8_t *sha256_hash,
                                                const char *signature_b64);

/**
 * @brief Get status code as human-readable string
 *
 * @param status Status code
 * @return String description
 */
const char* serv_signature_verify_status_str(sig_verify_status_t status);

#endif // SERV_SIGNATURE_VERIFY_H
