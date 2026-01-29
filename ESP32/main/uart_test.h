/**
 * @file uart_test.h
 * @brief UART test for logic analyzer validation
 */

#ifndef UART_TEST_H
#define UART_TEST_H

/**
 * @brief Run UART test patterns
 *
 * Continuously sends test patterns on UART1 for logic analyzer verification.
 * Does not return - call from app_main or a dedicated task.
 */
void uart_test_run(void);

#endif // UART_TEST_H
