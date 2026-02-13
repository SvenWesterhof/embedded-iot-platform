/**
 * @file serv_signature_verify.h
 * @brief Signature Verification Service - ED25519 signature verification for STM32 firmware
 *
 * Verifies ED25519 signatures for STM32 firmware downloads.
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
    SIG_ALG_ED25519,
    SIG_ALG_RSA_3072,  // Future expansion
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
 * @brief Verify ED25519 signature for STM32 firmware
 *
 * @param firmware_data Pointer to firmware binary
 * @param firmware_size Size of firmware in bytes
 * @param signature_b64 Base64-encoded ED25519 signature (64 bytes decoded)
 * @return SIG_VERIFY_OK if signature is valid, error code otherwise
 */
sig_verify_status_t serv_signature_verify_stm32(const uint8_t *firmware_data,
                                                uint32_t firmware_size,
                                                const char *signature_b64);

/**
 * @brief Get status code as human-readable string
 *
 * @param status Status code
 * @return String description
 */
const char* serv_signature_verify_status_str(sig_verify_status_t status);

#endif // SERV_SIGNATURE_VERIFY_H
