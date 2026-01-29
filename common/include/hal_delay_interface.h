/**
 * @file hal_delay_interface.h
 * @brief Hardware Abstraction Layer interface for delays and timing
 *
 * Defines platform-independent delay/timing interface using Strategy pattern.
 */

#ifndef HAL_DELAY_INTERFACE_H
#define HAL_DELAY_INTERFACE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Delay HAL Interface
// ============================================================================

/**
 * @brief Delay/Timing Hardware Abstraction Layer interface
 */
typedef struct {
    /**
     * @brief Initialize delay/timing HAL
     * @return true if successful
     */
    bool (*init)(void);

    /**
     * @brief Delay for specified milliseconds
     * @param ms Milliseconds to delay
     *
     * @note This function blocks execution. In RTOS environment,
     *       this should yield to other tasks.
     */
    void (*delay_ms)(uint32_t ms);

    /**
     * @brief Delay for specified microseconds
     * @param us Microseconds to delay
     *
     * @note Short delays may busy-wait to maintain accuracy.
     */
    void (*delay_us)(uint32_t us);

    /**
     * @brief Get current system tick count
     * @return Tick count in milliseconds since system start
     *
     * @note Wraps around after ~49.7 days (32-bit milliseconds).
     */
    uint32_t (*get_tick_ms)(void);

    /**
     * @brief Get current system tick count in microseconds
     * @return Tick count in microseconds since system start
     *
     * @note Wraps around after ~71.5 minutes (32-bit microseconds).
     *       Use for short-duration timing only.
     */
    uint32_t (*get_tick_us)(void);

    /**
     * @brief Get elapsed time since a previous tick
     * @param start_tick Previous tick value from get_tick_ms()
     * @return Elapsed milliseconds (handles wrap-around)
     */
    uint32_t (*elapsed_ms)(uint32_t start_tick);

    /**
     * @brief Check if timeout has expired
     * @param start_tick Start time from get_tick_ms()
     * @param timeout_ms Timeout duration in milliseconds
     * @return true if timeout expired, false otherwise
     */
    bool (*is_timeout)(uint32_t start_tick, uint32_t timeout_ms);

} hal_delay_interface_t;

// ============================================================================
// Global HAL Instance
// ============================================================================

/**
 * @brief Global delay/timing HAL interface pointer
 */
extern const hal_delay_interface_t* hal_delay;

// ============================================================================
// Helper Macros
// ============================================================================

#define hal_delay_init()                hal_delay->init()
#define hal_delay_milliseconds(ms)      hal_delay->delay_ms(ms)
#define hal_delay_microseconds(us)      hal_delay->delay_us(us)
#define hal_get_tick_count()            hal_delay->get_tick_ms()
#define hal_get_tick_us_count()         hal_delay->get_tick_us()

#ifdef __cplusplus
}
#endif

#endif // HAL_DELAY_INTERFACE_H
