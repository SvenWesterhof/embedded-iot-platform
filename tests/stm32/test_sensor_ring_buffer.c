/**
 * @file test_sensor_ring_buffer.c
 * @brief Host-native unit tests for the STM32 sensor ring buffer.
 *
 * Build and run from repo root:
 *   cmake -S tests -B tests/build -G Ninja
 *   cmake --build tests/build
 *   cd tests/build && ctest --output-on-failure
 *
 * The os_wrapper mutex functions are mocked via CMock — the ring buffer logic
 * is exercised without any FreeRTOS dependency.
 */

#include "unity.h"
#include "mock_os_wrapper.h"
#include "sensor_ring_buffer.h"
#include <string.h>

/* -------------------------------------------------------------------------
 * Test fixtures
 * ------------------------------------------------------------------------- */

#define FAKE_MUTEX  ((os_mutex_handle_t)(uintptr_t)0xDEAD)
#define SMALL_CAP   4u   /* small capacity to make wrap-around tests fast */

static sensor_ring_buffer_t rb;

static sensor_sample_t make_sample(uint32_t timestamp, int32_t value)
{
    sensor_sample_t s;
    s.sensor_type = SENSOR_TEMPERATURE;
    s.timestamp   = timestamp;
    s.value       = value;
    return s;
}

static void push_sequence(uint32_t n)
{
    for (uint32_t i = 1; i <= n; i++) {
        sensor_sample_t s = make_sample(i, (int32_t)i * 10);
        sensor_ring_buffer_push(&rb, &s);
    }
}

/* -------------------------------------------------------------------------
 * setUp / tearDown
 * ------------------------------------------------------------------------- */

void setUp(void)
{
    mock_os_wrapper_Init();
    memset(&rb, 0, sizeof(rb));

    /* Happy-path defaults: mutex always succeeds. Individual tests can
     * override these by calling _IgnoreAndReturn again before the CUT call. */
    os_mutex_create_IgnoreAndReturn(FAKE_MUTEX);
    os_mutex_take_IgnoreAndReturn(OS_SUCCESS);
    os_mutex_give_IgnoreAndReturn(OS_SUCCESS);
    os_mutex_delete_Ignore();
}

void tearDown(void)
{
    if (rb.initialized) {
        sensor_ring_buffer_deinit(&rb);
    }
    mock_os_wrapper_Verify();
    mock_os_wrapper_Destroy();
}

/* Helper: init the shared rb with a given capacity */
static void init_rb(uint32_t cap)
{
    sensor_ring_buffer_config_t cfg = { .capacity = cap, .sensor_type = SENSOR_TEMPERATURE };
    TEST_ASSERT_EQUAL(SENSOR_RING_BUFFER_OK, sensor_ring_buffer_init(&rb, &cfg));
}

/* =========================================================================
 * Init / Deinit
 * ========================================================================= */

void test_init_null_rb_returns_invalid_arg(void)
{
    TEST_ASSERT_EQUAL(SENSOR_RING_BUFFER_ERR_INVALID_ARG,
                      sensor_ring_buffer_init(NULL, NULL));
}

void test_init_null_config_uses_defaults(void)
{
    TEST_ASSERT_EQUAL(SENSOR_RING_BUFFER_OK, sensor_ring_buffer_init(&rb, NULL));
    TEST_ASSERT_TRUE(sensor_ring_buffer_is_initialized(&rb));
    TEST_ASSERT_EQUAL(SENSOR_RING_BUFFER_DEFAULT_CAPACITY,
                      sensor_ring_buffer_get_capacity(&rb));
}

void test_init_zero_capacity_uses_default(void)
{
    sensor_ring_buffer_config_t cfg = { .capacity = 0, .sensor_type = SENSOR_TEMPERATURE };
    TEST_ASSERT_EQUAL(SENSOR_RING_BUFFER_OK, sensor_ring_buffer_init(&rb, &cfg));
    TEST_ASSERT_EQUAL(SENSOR_RING_BUFFER_DEFAULT_CAPACITY,
                      sensor_ring_buffer_get_capacity(&rb));
}

void test_init_already_initialized_returns_error(void)
{
    init_rb(SMALL_CAP);
    TEST_ASSERT_EQUAL(SENSOR_RING_BUFFER_ERR_ALREADY_INIT,
                      sensor_ring_buffer_init(&rb, NULL));
}

void test_init_mutex_create_fail_returns_no_mem(void)
{
    /* _IgnoreAndReturn queues values (FIFO), so setUp's FAKE_MUTEX would be
     * consumed first. Reset mocks to get a clean queue, then register NULL. */
    mock_os_wrapper_Destroy();
    mock_os_wrapper_Init();
    os_mutex_create_IgnoreAndReturn(NULL);

    sensor_ring_buffer_config_t cfg = { .capacity = SMALL_CAP, .sensor_type = SENSOR_TEMPERATURE };
    TEST_ASSERT_EQUAL(SENSOR_RING_BUFFER_ERR_NO_MEM,
                      sensor_ring_buffer_init(&rb, &cfg));
    TEST_ASSERT_FALSE(sensor_ring_buffer_is_initialized(&rb));
}

void test_deinit_null_rb_returns_invalid_arg(void)
{
    TEST_ASSERT_EQUAL(SENSOR_RING_BUFFER_ERR_INVALID_ARG,
                      sensor_ring_buffer_deinit(NULL));
}

void test_deinit_not_initialized_returns_error(void)
{
    TEST_ASSERT_EQUAL(SENSOR_RING_BUFFER_ERR_NOT_INIT,
                      sensor_ring_buffer_deinit(&rb));
}

void test_deinit_clears_state(void)
{
    init_rb(SMALL_CAP);
    push_sequence(2);

    TEST_ASSERT_EQUAL(SENSOR_RING_BUFFER_OK, sensor_ring_buffer_deinit(&rb));

    TEST_ASSERT_FALSE(sensor_ring_buffer_is_initialized(&rb));
    TEST_ASSERT_EQUAL(0, sensor_ring_buffer_get_count(&rb));
    /* tearDown won't call deinit again since initialized is now false */
}

/* =========================================================================
 * Push / Count
 * ========================================================================= */

void test_push_null_rb_returns_invalid_arg(void)
{
    sensor_sample_t s = make_sample(1, 100);
    TEST_ASSERT_EQUAL(SENSOR_RING_BUFFER_ERR_INVALID_ARG,
                      sensor_ring_buffer_push(NULL, &s));
}

void test_push_null_sample_returns_invalid_arg(void)
{
    init_rb(SMALL_CAP);
    TEST_ASSERT_EQUAL(SENSOR_RING_BUFFER_ERR_INVALID_ARG,
                      sensor_ring_buffer_push(&rb, NULL));
}

void test_push_not_initialized_returns_error(void)
{
    sensor_sample_t s = make_sample(1, 100);
    TEST_ASSERT_EQUAL(SENSOR_RING_BUFFER_ERR_NOT_INIT,
                      sensor_ring_buffer_push(&rb, &s));
}

void test_push_increments_count(void)
{
    init_rb(SMALL_CAP);
    sensor_sample_t s = make_sample(1, 100);

    sensor_ring_buffer_push(&rb, &s);
    TEST_ASSERT_EQUAL(1, sensor_ring_buffer_get_count(&rb));

    sensor_ring_buffer_push(&rb, &s);
    TEST_ASSERT_EQUAL(2, sensor_ring_buffer_get_count(&rb));
}

void test_push_fills_to_capacity(void)
{
    init_rb(SMALL_CAP);
    push_sequence(SMALL_CAP);
    TEST_ASSERT_EQUAL(SMALL_CAP, sensor_ring_buffer_get_count(&rb));
}

void test_push_beyond_capacity_count_stays_at_capacity(void)
{
    init_rb(SMALL_CAP);
    push_sequence(SMALL_CAP + 3);
    TEST_ASSERT_EQUAL(SMALL_CAP, sensor_ring_buffer_get_count(&rb));
}

/* =========================================================================
 * Peek
 * ========================================================================= */

void test_peek_empty_returns_error(void)
{
    init_rb(SMALL_CAP);
    sensor_sample_t out;
    TEST_ASSERT_EQUAL(SENSOR_RING_BUFFER_ERR_EMPTY,
                      sensor_ring_buffer_peek(&rb, 0, &out));
}

void test_peek_out_of_range_returns_invalid_arg(void)
{
    init_rb(SMALL_CAP);
    push_sequence(2);
    sensor_sample_t out;
    TEST_ASSERT_EQUAL(SENSOR_RING_BUFFER_ERR_INVALID_ARG,
                      sensor_ring_buffer_peek(&rb, 2, &out));
}

void test_peek_index0_is_oldest(void)
{
    init_rb(SMALL_CAP);
    push_sequence(3);   /* ts=1, ts=2, ts=3 */

    sensor_sample_t out;
    TEST_ASSERT_EQUAL(SENSOR_RING_BUFFER_OK, sensor_ring_buffer_peek(&rb, 0, &out));
    TEST_ASSERT_EQUAL(1, out.timestamp);
    TEST_ASSERT_EQUAL(10, out.value);
}

void test_peek_last_index_is_newest(void)
{
    init_rb(SMALL_CAP);
    push_sequence(3);   /* ts=1, ts=2, ts=3 */

    sensor_sample_t out;
    TEST_ASSERT_EQUAL(SENSOR_RING_BUFFER_OK, sensor_ring_buffer_peek(&rb, 2, &out));
    TEST_ASSERT_EQUAL(3, out.timestamp);
    TEST_ASSERT_EQUAL(30, out.value);
}

/* =========================================================================
 * Read
 * ========================================================================= */

void test_read_empty_returns_error(void)
{
    init_rb(SMALL_CAP);
    sensor_sample_t out[SMALL_CAP];
    uint32_t n;
    TEST_ASSERT_EQUAL(SENSOR_RING_BUFFER_ERR_EMPTY,
                      sensor_ring_buffer_read(&rb, 0, out, SMALL_CAP, &n));
}

void test_read_start_index_out_of_range_returns_invalid_arg(void)
{
    init_rb(SMALL_CAP);
    push_sequence(2);
    sensor_sample_t out[SMALL_CAP];
    uint32_t n;
    TEST_ASSERT_EQUAL(SENSOR_RING_BUFFER_ERR_INVALID_ARG,
                      sensor_ring_buffer_read(&rb, 2, out, SMALL_CAP, &n));
}

void test_read_all_samples_oldest_first(void)
{
    init_rb(SMALL_CAP);
    push_sequence(3);   /* ts=1,2,3 */

    sensor_sample_t out[SMALL_CAP];
    uint32_t n;
    TEST_ASSERT_EQUAL(SENSOR_RING_BUFFER_OK,
                      sensor_ring_buffer_read(&rb, 0, out, SMALL_CAP, &n));
    TEST_ASSERT_EQUAL(3, n);
    TEST_ASSERT_EQUAL(1, out[0].timestamp);
    TEST_ASSERT_EQUAL(2, out[1].timestamp);
    TEST_ASSERT_EQUAL(3, out[2].timestamp);
}

void test_read_respects_max_samples(void)
{
    init_rb(SMALL_CAP);
    push_sequence(SMALL_CAP);

    sensor_sample_t out[2];
    uint32_t n;
    TEST_ASSERT_EQUAL(SENSOR_RING_BUFFER_OK,
                      sensor_ring_buffer_read(&rb, 0, out, 2, &n));
    TEST_ASSERT_EQUAL(2, n);
}

void test_read_with_start_index_skips_older_samples(void)
{
    init_rb(SMALL_CAP);
    push_sequence(SMALL_CAP);   /* ts=1,2,3,4 */

    sensor_sample_t out[SMALL_CAP];
    uint32_t n;
    TEST_ASSERT_EQUAL(SENSOR_RING_BUFFER_OK,
                      sensor_ring_buffer_read(&rb, 2, out, SMALL_CAP, &n));
    TEST_ASSERT_EQUAL(2, n);
    TEST_ASSERT_EQUAL(3, out[0].timestamp);
    TEST_ASSERT_EQUAL(4, out[1].timestamp);
}

/* =========================================================================
 * Circular wrap-around (the critical behaviour)
 * ========================================================================= */

void test_circular_oldest_overwritten_on_overflow(void)
{
    /* capacity=4, push 5 — ts=1 is dropped, oldest becomes ts=2 */
    init_rb(SMALL_CAP);
    push_sequence(SMALL_CAP + 1);

    TEST_ASSERT_EQUAL(SMALL_CAP, sensor_ring_buffer_get_count(&rb));

    sensor_sample_t oldest;
    TEST_ASSERT_EQUAL(SENSOR_RING_BUFFER_OK,
                      sensor_ring_buffer_peek(&rb, 0, &oldest));
    TEST_ASSERT_EQUAL(2, oldest.timestamp);
}

void test_circular_read_order_preserved_after_overflow(void)
{
    /* capacity=4, push 6 — buffer holds ts=3,4,5,6 in FIFO order */
    init_rb(SMALL_CAP);
    push_sequence(6);

    sensor_sample_t out[SMALL_CAP];
    uint32_t n;
    sensor_ring_buffer_read(&rb, 0, out, SMALL_CAP, &n);

    TEST_ASSERT_EQUAL(SMALL_CAP, n);
    TEST_ASSERT_EQUAL(3, out[0].timestamp);
    TEST_ASSERT_EQUAL(4, out[1].timestamp);
    TEST_ASSERT_EQUAL(5, out[2].timestamp);
    TEST_ASSERT_EQUAL(6, out[3].timestamp);
}

void test_circular_head_wraps_past_buffer_boundary(void)
{
    /* capacity=3, push 5 — head wraps to slot 2, buffer holds ts=3,4,5 */
    sensor_ring_buffer_config_t cfg = { .capacity = 3, .sensor_type = SENSOR_TEMPERATURE };
    TEST_ASSERT_EQUAL(SENSOR_RING_BUFFER_OK, sensor_ring_buffer_init(&rb, &cfg));
    push_sequence(5);

    sensor_sample_t out[3];
    uint32_t n;
    sensor_ring_buffer_read(&rb, 0, out, 3, &n);

    TEST_ASSERT_EQUAL(3, n);
    TEST_ASSERT_EQUAL(3, out[0].timestamp);
    TEST_ASSERT_EQUAL(4, out[1].timestamp);
    TEST_ASSERT_EQUAL(5, out[2].timestamp);
}

/* =========================================================================
 * Clear
 * ========================================================================= */

void test_clear_resets_count_to_zero(void)
{
    init_rb(SMALL_CAP);
    push_sequence(3);

    TEST_ASSERT_EQUAL(SENSOR_RING_BUFFER_OK, sensor_ring_buffer_clear(&rb));
    TEST_ASSERT_EQUAL(0, sensor_ring_buffer_get_count(&rb));
}

void test_clear_allows_buffer_reuse(void)
{
    init_rb(SMALL_CAP);
    push_sequence(3);
    sensor_ring_buffer_clear(&rb);

    sensor_sample_t fresh = make_sample(999, 42);
    sensor_ring_buffer_push(&rb, &fresh);

    TEST_ASSERT_EQUAL(1, sensor_ring_buffer_get_count(&rb));

    sensor_sample_t out;
    sensor_ring_buffer_peek(&rb, 0, &out);
    TEST_ASSERT_EQUAL(999, out.timestamp);
    TEST_ASSERT_EQUAL(42, out.value);
}

void test_clear_on_uninitialized_returns_error(void)
{
    TEST_ASSERT_EQUAL(SENSOR_RING_BUFFER_ERR_NOT_INIT,
                      sensor_ring_buffer_clear(&rb));
}

/* =========================================================================
 * Entry point
 * ========================================================================= */

int main(void)
{
    UNITY_BEGIN();

    /* Init / Deinit */
    RUN_TEST(test_init_null_rb_returns_invalid_arg);
    RUN_TEST(test_init_null_config_uses_defaults);
    RUN_TEST(test_init_zero_capacity_uses_default);
    RUN_TEST(test_init_already_initialized_returns_error);
    RUN_TEST(test_init_mutex_create_fail_returns_no_mem);
    RUN_TEST(test_deinit_null_rb_returns_invalid_arg);
    RUN_TEST(test_deinit_not_initialized_returns_error);
    RUN_TEST(test_deinit_clears_state);

    /* Push / Count */
    RUN_TEST(test_push_null_rb_returns_invalid_arg);
    RUN_TEST(test_push_null_sample_returns_invalid_arg);
    RUN_TEST(test_push_not_initialized_returns_error);
    RUN_TEST(test_push_increments_count);
    RUN_TEST(test_push_fills_to_capacity);
    RUN_TEST(test_push_beyond_capacity_count_stays_at_capacity);

    /* Peek */
    RUN_TEST(test_peek_empty_returns_error);
    RUN_TEST(test_peek_out_of_range_returns_invalid_arg);
    RUN_TEST(test_peek_index0_is_oldest);
    RUN_TEST(test_peek_last_index_is_newest);

    /* Read */
    RUN_TEST(test_read_empty_returns_error);
    RUN_TEST(test_read_start_index_out_of_range_returns_invalid_arg);
    RUN_TEST(test_read_all_samples_oldest_first);
    RUN_TEST(test_read_respects_max_samples);
    RUN_TEST(test_read_with_start_index_skips_older_samples);

    /* Circular wrap-around */
    RUN_TEST(test_circular_oldest_overwritten_on_overflow);
    RUN_TEST(test_circular_read_order_preserved_after_overflow);
    RUN_TEST(test_circular_head_wraps_past_buffer_boundary);

    /* Clear */
    RUN_TEST(test_clear_resets_count_to_zero);
    RUN_TEST(test_clear_allows_buffer_reuse);
    RUN_TEST(test_clear_on_uninitialized_returns_error);

    return UNITY_END();
}
