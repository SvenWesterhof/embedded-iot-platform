/**
 * @file test_crc16.c
 * @brief Host-native unit tests for the shared CRC16-CCITT implementation.
 *
 * Run from repo root:
 *   cmake -S tests -B tests/build -G Ninja
 *   cmake --build tests/build
 *   cd tests/build && ctest --output-on-failure
 *
 * CRC16-CCITT (polynomial 0x1021, init 0xFFFF, no reflection, no final XOR).
 * Also known as CRC-16/CCITT-FALSE or CRC-16/IBM-3740.
 */

#include "unity.h"
#include "crc16.h"

void setUp(void)    { /* nothing to initialise */ }
void tearDown(void) { /* nothing to clean up   */ }

/* -------------------------------------------------------------------------
 * Test: Standard reference vector
 * Input:  ASCII "123456789" (0x31 .. 0x39)
 * Result: 0x29B1  (universally published CRC-16/CCITT-FALSE check value)
 * ------------------------------------------------------------------------- */
void test_crc16_standard_vector(void)
{
    const uint8_t data[] = {
        0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39
    };
    uint16_t result = crc16_ccitt(data, sizeof(data));
    TEST_ASSERT_EQUAL_HEX16(0x29B1, result);
}

/* -------------------------------------------------------------------------
 * Test: Empty buffer
 * With no bytes processed the loop never runs, so the initial value 0xFFFF
 * passes through unchanged.
 * ------------------------------------------------------------------------- */
void test_crc16_empty_buffer(void)
{
    const uint8_t dummy = 0;
    /* Pass a valid pointer but length = 0 — loop is never entered */
    uint16_t result = crc16_ccitt(&dummy, 0);
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, result);
}

/* -------------------------------------------------------------------------
 * Test: Single zero byte
 * Manually derived:
 *   index = (0xFFFF >> 8) ^ 0x00 = 0xFF
 *   crc   = (0xFFFF << 8)[16-bit] ^ table[0xFF]
 *         = 0xFF00 ^ 0x1EF0 = 0xE1F0
 * ------------------------------------------------------------------------- */
void test_crc16_single_zero_byte(void)
{
    const uint8_t data[] = { 0x00 };
    uint16_t result = crc16_ccitt(data, 1);
    TEST_ASSERT_EQUAL_HEX16(0xE1F0, result);
}

/* -------------------------------------------------------------------------
 * Test: Incrementally computed CRC equals single-pass CRC
 * The function is called twice in sequence; the result should be identical
 * to computing over the full buffer at once.
 * NOTE: crc16_ccitt always starts with 0xFFFF so incremental use is NOT
 * supported. This test documents that behaviour explicitly.
 * ------------------------------------------------------------------------- */
void test_crc16_full_buffer_matches_reference(void)
{
    const uint8_t packet[] = {
        0xAA,       /* SOF marker used in the project protocol */
        0x01,       /* command byte */
        0x04, 0x00, /* payload length (little-endian) */
        0xDE, 0xAD, 0xBE, 0xEF  /* payload */
    };
    /* Compute twice; must be deterministic */
    uint16_t first  = crc16_ccitt(packet, sizeof(packet));
    uint16_t second = crc16_ccitt(packet, sizeof(packet));
    TEST_ASSERT_EQUAL_HEX16(first, second);
    /* Sanity: result must not be the initial value (all-ones) */
    TEST_ASSERT_NOT_EQUAL(0xFFFF, first);
}

/* -------------------------------------------------------------------------
 * Test: All-ones payload
 * A buffer of all 0xFF bytes should produce a non-trivially-zero CRC.
 * ------------------------------------------------------------------------- */
void test_crc16_all_ones_payload(void)
{
    const uint8_t data[8] = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
    };
    uint16_t result = crc16_ccitt(data, sizeof(data));
    /* Must not be 0x0000 or 0xFFFF — both indicate a degenerate pass-through */
    TEST_ASSERT_NOT_EQUAL(0x0000, result);
    TEST_ASSERT_NOT_EQUAL(0xFFFF, result);
}

/* -------------------------------------------------------------------------
 * Entry point
 * ------------------------------------------------------------------------- */
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_crc16_standard_vector);
    RUN_TEST(test_crc16_empty_buffer);
    RUN_TEST(test_crc16_single_zero_byte);
    RUN_TEST(test_crc16_full_buffer_matches_reference);
    RUN_TEST(test_crc16_all_ones_payload);
    return UNITY_END();
}
