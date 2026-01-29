#ifndef OS_WRAPPER_H
#define OS_WRAPPER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations for opaque handles
typedef void* os_queue_handle_t;
typedef void* os_task_handle_t;
typedef void* os_mutex_handle_t;
typedef void* os_semaphore_handle_t;

// Task function prototype
typedef void (*os_task_func_t)(void* args);

// Return codes
typedef enum {
    OS_SUCCESS = 0,
    OS_ERROR,
    OS_TIMEOUT,
    OS_FULL,
    OS_EMPTY,
    OS_INVALID_PARAM,
    OS_NO_MEMORY
} os_result_t;

// Constants
#define OS_WAIT_FOREVER     0xFFFFFFFF
#define OS_NO_WAIT          0

// Core affinity constants (for os_task_create_pinned)
#define OS_CORE_0           0           // CPU core 0
#define OS_CORE_1           1           // CPU core 1 (if available)
#define OS_CORE_ANY         0           // Let scheduler choose (use on single-core)

// Priority level suggestions (adjust based on FreeRTOS configMAX_PRIORITIES)
// Typical range: 0 (lowest) to 24 (highest) on ESP32
//                0 (lowest) to configMAX_PRIORITIES-1 on STM32
#define OS_PRIORITY_IDLE            0   // Idle priority
#define OS_PRIORITY_LOW             3   // Background tasks
#define OS_PRIORITY_NORMAL          5   // Standard priority
#define OS_PRIORITY_HIGH            10  // Time-sensitive tasks
#define OS_PRIORITY_CRITICAL        15  // Critical real-time tasks
#define OS_PRIORITY_ISR_DEFERRED    20  // Deferred ISR processing

// =============================================================================
// INITIALIZATION
// =============================================================================

/**
 * @brief Initialize the OS wrapper
 * @return OS_SUCCESS on success, OS_ERROR on failure
 */
os_result_t os_init(void);

// =============================================================================
// QUEUE OPERATIONS
// =============================================================================

/**
 * @brief Create a message queue
 * @param queue_length Maximum number of items the queue can hold
 * @param item_size Size of each item in bytes
 * @return Queue handle on success, NULL on failure
 */
os_queue_handle_t os_queue_create(uint32_t queue_length, uint32_t item_size);

/**
 * @brief Delete a queue
 * @param queue Queue handle to delete
 */
void os_queue_delete(os_queue_handle_t queue);

/**
 * @brief Send an item to a queue
 * @param queue Queue handle
 * @param item Pointer to item to send
 * @param timeout_ms Timeout in milliseconds (OS_NO_WAIT, OS_WAIT_FOREVER, or specific time)
 * @return OS_SUCCESS, OS_TIMEOUT, OS_FULL, or OS_INVALID_PARAM
 * 
 * @note BLOCKING: This function may block if timeout_ms > 0 and queue is full.
 *       Use OS_NO_WAIT for non-blocking operation.
 *       Must NOT be called from ISR context - use os_queue_send_from_isr() instead.
 */
os_result_t os_queue_send(os_queue_handle_t queue, const void* item, uint32_t timeout_ms);

/**
 * @brief Send an item to a queue from ISR context
 * @param queue Queue handle
 * @param item Pointer to item to send
 * @param higher_priority_task_woken Set to true if a higher priority task was woken
 * @return OS_SUCCESS, OS_FULL, or OS_INVALID_PARAM
 * 
 * @note NON-BLOCKING: This function never blocks and should only be called from ISR context.
 *       No timeout parameter - always returns immediately.
 */
os_result_t os_queue_send_from_isr(os_queue_handle_t queue, const void* item, bool* higher_priority_task_woken);

/**
 * @brief Receive an item from a queue
 * @param queue Queue handle
 * @param item Pointer to buffer to receive item
 * @param timeout_ms Timeout in milliseconds (OS_NO_WAIT, OS_WAIT_FOREVER, or specific time)
 * @return OS_SUCCESS, OS_TIMEOUT, OS_EMPTY, or OS_INVALID_PARAM
 * 
 * @note BLOCKING: This function may block if timeout_ms > 0 and queue is empty.
 *       Use OS_NO_WAIT for non-blocking operation.
 *       Must NOT be called from ISR context - use os_queue_receive_from_isr() instead.
 */
os_result_t os_queue_receive(os_queue_handle_t queue, void* item, uint32_t timeout_ms);

/**
 * @brief Receive an item from a queue from ISR context
 * @param queue Queue handle
 * @param item Pointer to buffer to receive item
 * @param higher_priority_task_woken Set to true if a higher priority task was woken
 * @return OS_SUCCESS, OS_EMPTY, or OS_INVALID_PARAM
 * 
 * @note NON-BLOCKING: This function never blocks and should only be called from ISR context.
 *       No timeout parameter - always returns immediately.
 */
os_result_t os_queue_receive_from_isr(os_queue_handle_t queue, void* item, bool* higher_priority_task_woken);

/**
 * @brief Get the number of items currently in the queue
 * @param queue Queue handle
 * @return Number of items in queue, 0 if queue is NULL
 */
uint32_t os_queue_get_count(os_queue_handle_t queue);

/**
 * @brief Reset a queue (flush all items)
 * @param queue Queue handle
 * @return OS_SUCCESS on success, OS_INVALID_PARAM if queue is NULL
 */
os_result_t os_queue_reset(os_queue_handle_t queue);

// =============================================================================
// TASK OPERATIONS
// =============================================================================

/**
 * @brief Create a task
 * @param task_func Task function to execute
 * @param name Task name (for debugging)
 * @param stack_size Stack size in bytes (converted to words internally)
 * @param params Parameters to pass to task function
 * @param priority Task priority (0=lowest, higher number = higher priority)
 *                 Use OS_PRIORITY_* constants for portability
 * @param handle Pointer to store task handle (can be NULL)
 * @return OS_SUCCESS on success, OS_ERROR or OS_NO_MEMORY on failure
 * 
 * @note Stack size is in BYTES and converted internally to platform-specific units.
 *       Priority range depends on FreeRTOS configuration (typically 0-24).
 */
os_result_t os_task_create(os_task_func_t task_func, const char* name, 
                          uint32_t stack_size, void* params, 
                          uint8_t priority, os_task_handle_t* handle);

/**
 * @brief Create a task pinned to a specific CPU core (multi-core systems)
 * @param task_func Task function to execute
 * @param name Task name (for debugging)
 * @param stack_size Stack size in bytes (converted to words internally)
 * @param params Parameters to pass to task function
 * @param priority Task priority (0=lowest, higher number = higher priority)
 *                 Use OS_PRIORITY_* constants for portability
 * @param handle Pointer to store task handle (can be NULL)
 * @param core_id CPU core ID: OS_CORE_0, OS_CORE_1, or OS_CORE_ANY
 * @return OS_SUCCESS on success, OS_ERROR or OS_NO_MEMORY on failure
 * 
 * @note PORTABILITY: On single-core systems (STM32, ESP32-C3), core_id is ignored.
 *       The function behaves identically to os_task_create().
 *       Stack size is in BYTES and converted internally to platform-specific units.
 */
os_result_t os_task_create_pinned(os_task_func_t task_func, const char* name, 
                                 uint32_t stack_size, void* params, 
                                 uint8_t priority, os_task_handle_t* handle,
                                 uint8_t core_id);

/**
 * @brief Delete a task
 * @param handle Task handle to delete (NULL deletes current task)
 */
void os_task_delete(os_task_handle_t handle);

/**
 * @brief Get current task handle
 * @return Current task handle
 */
os_task_handle_t os_task_get_current(void);

// =============================================================================
// SYNCHRONIZATION - MUTEX
// =============================================================================

/**
 * @brief Create a mutex
 * @return Mutex handle on success, NULL on failure
 */
os_mutex_handle_t os_mutex_create(void);

/**
 * @brief Delete a mutex
 * @param mutex Mutex handle to delete
 */
void os_mutex_delete(os_mutex_handle_t mutex);

/**
 * @brief Take (lock) a mutex
 * @param mutex Mutex handle
 * @param timeout_ms Timeout in milliseconds (OS_NO_WAIT, OS_WAIT_FOREVER, or specific time)
 * @return OS_SUCCESS, OS_TIMEOUT, or OS_INVALID_PARAM
 * 
 * @note BLOCKING: This function may block if timeout_ms > 0 and mutex is already taken.
 *       Use OS_NO_WAIT for non-blocking operation.
 *       Must NOT be called from ISR context - mutexes should not be used in ISRs.
 */
os_result_t os_mutex_take(os_mutex_handle_t mutex, uint32_t timeout_ms);

/**
 * @brief Give (unlock) a mutex
 * @param mutex Mutex handle
 * @return OS_SUCCESS or OS_INVALID_PARAM
 * 
 * @note NON-BLOCKING: This function never blocks.
 *       Must NOT be called from ISR context - mutexes should not be used in ISRs.
 */
os_result_t os_mutex_give(os_mutex_handle_t mutex);

// =============================================================================
// SYNCHRONIZATION - SEMAPHORE
// =============================================================================

/**
 * @brief Create a binary semaphore
 * @return Semaphore handle on success, NULL on failure
 */
os_semaphore_handle_t os_semaphore_create_binary(void);

/**
 * @brief Create a counting semaphore
 * @param max_count Maximum count value
 * @param initial_count Initial count value
 * @return Semaphore handle on success, NULL on failure
 */
os_semaphore_handle_t os_semaphore_create_counting(uint32_t max_count, uint32_t initial_count);

/**
 * @brief Delete a semaphore
 * @param semaphore Semaphore handle to delete
 */
void os_semaphore_delete(os_semaphore_handle_t semaphore);

/**
 * @brief Take (acquire) a semaphore
 * @param semaphore Semaphore handle
 * @param timeout_ms Timeout in milliseconds (OS_NO_WAIT, OS_WAIT_FOREVER, or specific time)
 * @return OS_SUCCESS, OS_TIMEOUT, or OS_INVALID_PARAM
 * 
 * @note BLOCKING: This function may block if timeout_ms > 0 and semaphore count is zero.
 *       Use OS_NO_WAIT for non-blocking operation.
 *       Must NOT be called from ISR context - use os_semaphore_take_from_isr() instead.
 */
os_result_t os_semaphore_take(os_semaphore_handle_t semaphore, uint32_t timeout_ms);

/**
 * @brief Give (release) a semaphore
 * @param semaphore Semaphore handle
 * @return OS_SUCCESS or OS_INVALID_PARAM
 * 
 * @note NON-BLOCKING: This function never blocks.
 *       Must NOT be called from ISR context - use os_semaphore_give_from_isr() instead.
 */
os_result_t os_semaphore_give(os_semaphore_handle_t semaphore);

/**
 * @brief Give (release) a semaphore from ISR context
 * @param semaphore Semaphore handle
 * @param higher_priority_task_woken Set to true if a higher priority task was woken
 * @return OS_SUCCESS or OS_INVALID_PARAM
 * 
 * @note NON-BLOCKING: This function never blocks and should only be called from ISR context.
 */
os_result_t os_semaphore_give_from_isr(os_semaphore_handle_t semaphore, bool* higher_priority_task_woken);

/**
 * @brief Take (acquire) a semaphore from ISR context
 * @param semaphore Semaphore handle
 * @param higher_priority_task_woken Set to true if a higher priority task was woken
 * @return OS_SUCCESS, OS_TIMEOUT, or OS_INVALID_PARAM
 *
 * @note NON-BLOCKING: This function never blocks and should only be called from ISR context.
 *       No timeout parameter - always returns immediately.
 */
os_result_t os_semaphore_take_from_isr(os_semaphore_handle_t semaphore, bool* higher_priority_task_woken);

// =============================================================================
// ISR UTILITIES
// =============================================================================

/**
 * @brief Yield to higher priority task from ISR context
 * @param higher_priority_task_woken true if a higher priority task was woken by an ISR operation
 *
 * @note Call this at the end of an ISR if any FromISR function indicated a higher priority
 *       task was woken. This triggers an immediate context switch to that task.
 *       Only call from ISR context.
 */
void os_yield_from_isr(bool higher_priority_task_woken);

// =============================================================================
// TIME OPERATIONS
// =============================================================================

/**
 * @brief Get current system tick count
 * @return Current tick count
 */
uint32_t os_get_tick_count(void);

/**
 * @brief Get tick count in milliseconds
 * @return Current time in milliseconds
 */
uint32_t os_get_time_ms(void);

/**
 * @brief Delay current task for specified milliseconds
 * @param delay_ms Delay in milliseconds
 * 
 * @note BLOCKING: This function always blocks the calling task for the specified duration.
 *       Must NOT be called from ISR context.
 */
void os_delay_ms(uint32_t delay_ms);

/**
 * @brief Convert milliseconds to OS ticks
 * @param ms Milliseconds to convert
 * @return Equivalent tick count
 */
uint32_t os_ms_to_ticks(uint32_t ms);

/**
 * @brief Convert OS ticks to milliseconds
 * @param ticks Ticks to convert
 * @return Equivalent milliseconds
 */
uint32_t os_ticks_to_ms(uint32_t ticks);

#ifdef __cplusplus
}
#endif

#endif // OS_WRAPPER_H