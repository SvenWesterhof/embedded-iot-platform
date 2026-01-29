/**
 * @file systemview_demo.c
 * @brief SEGGER SystemView demonstration with RTT logging integration
 */

#include "SEGGER_SYSVIEW.h"
#include "SEGGER_RTT.h"
#include "portable_log.h"
#include "FreeRTOS.h"
#include "task.h"

static const char *TAG = "SYSVIEW";

/**
 * @brief Demonstrate SystemView instrumentation with RTT logging
 *
 * This shows how RTT logging and SystemView can work together
 */
void systemview_demo_start(void)
{
#if USE_SEGGER_SYSTEMVIEW
    LOG_I(TAG, "Starting SystemView demonstration");

    // Mark application milestones
    SEGGER_SYSVIEW_Print("Application Phase: Initialization Complete");

    LOG_I(TAG, "SystemView instrumentation active");
#else
    LOG_I(TAG, "SystemView disabled - skipping demonstration");
#endif
}

/**
 * @brief Log task switching events
 *
 * Call this periodically to show task activity in SystemView
 */
void systemview_demo_log_tasks(void)
{
#if USE_SEGGER_SYSTEMVIEW
    static uint32_t counter = 0;

    // SystemView will automatically track task switches
    // We can add custom events to mark specific application events
    if (counter % 1000 == 0) {  // Reduced frequency from 100 to 1000
        SEGGER_SYSVIEW_PrintfHost("Task monitoring checkpoint #%lu", counter);
        LOG_I(TAG, "Task monitoring checkpoint #%lu", counter);
    }

    counter++;
#endif
}

/**
 * @brief Mark critical application events for SystemView timeline
 */
void systemview_mark_event(const char* event_name)
{
#if USE_SEGGER_SYSTEMVIEW
    SEGGER_SYSVIEW_Print(event_name);
    LOG_D(TAG, "SystemView event: %s", event_name);
#else
    (void)event_name; // Suppress unused parameter warning
#endif
}