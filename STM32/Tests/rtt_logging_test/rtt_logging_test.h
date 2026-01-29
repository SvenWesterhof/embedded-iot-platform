/**
 * @file rtt_logging_test.h
 * @brief RTT logging functionality test and demonstration
 */

#ifndef RTT_LOGGING_TEST_H
#define RTT_LOGGING_TEST_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Test RTT logging functionality
 * 
 * This function demonstrates various RTT logging capabilities:
 * - Direct RTT calls
 * - Log macros (LOG_I, LOG_W, LOG_E)
 * - Formatted output
 * - Buffer output
 */
void rtt_test_logging(void);

/**
 * @brief RTT performance test
 * 
 * Measures RTT throughput and timing
 */
void rtt_test_performance(void);

/**
 * @brief Continuous RTT output for testing
 * 
 * Call this from a task to generate periodic log output
 */
void rtt_test_continuous(void);

#ifdef __cplusplus
}
#endif

#endif // RTT_LOGGING_TEST_H