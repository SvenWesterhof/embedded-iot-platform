/**
 * @file stm32_public_key.h
 * @brief Embedded RSA Public Key for Firmware Verification
 *
 * This file contains the embedded RSA-2048 public key for firmware signature verification.
 *
 * IMPORTANT: This is a PLACEHOLDER key for development/testing.
 * Replace with your actual production RSA public key before deployment.
 *
 * To generate a new RSA-2048 keypair:
 *   openssl genrsa -out private_key.pem 2048
 *   openssl rsa -in private_key.pem -pubout -out public_key.pem
 *
 * Then copy the contents of public_key.pem here.
 */

#ifndef STM32_PUBLIC_KEY_H
#define STM32_PUBLIC_KEY_H


// PLACEHOLDER RSA-2048 Public Key (replace with your production key)
static const char stm32_public_key_pem[] =
"-----BEGIN PUBLIC KEY-----\n"
"MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEA3IsUczzDZAtcWHmQihVC\n"
"R1mfsZrVNK0nDS0RwFGEeJBE1Hbkd72L572s61GjktI1kqw01dgmxnOSkNz1oQOA\n"
"5N83w4yGozeoavTwVdyM5vx6AQIabi5ydWvkxgBOGGFfKJpioQdK5JwwFJsY9zZi\n"
"pdAcX3k86M0cZxqp3iRoP3xzNpGJiQ4TXNB9PJNMEHlvxpbHBOXq7P66DISe2UFY\n"
"BE/7l7825I0OxJYW3VEURrwAlp7X8qUYluQuE3+HI9S7eTqw+Q6agHuEZQ4nO42b\n"
"S357HC6PXZGCr8S1rBvDAQriKWwj87vPSTFaUIKjS1Da47mQM7iKrwQt5ORu9ots\n"
"GwIDAQAB\n"
"-----END PUBLIC KEY-----\n";

#endif // STM32_PUBLIC_KEY_H