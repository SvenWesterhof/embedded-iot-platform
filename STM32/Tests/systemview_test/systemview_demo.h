/**
 * @file systemview_demo.h
 * @brief SEGGER SystemView demonstration with RTT logging integration
 */

#ifndef SYSTEMVIEW_DEMO_H
#define SYSTEMVIEW_DEMO_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Demonstrate SystemView instrumentation with RTT logging
 * 
 * This shows how RTT logging and SystemView can work together
 */
void systemview_demo_start(void);

/**
 * @brief Log task switching events
 * 
 * Call this periodically to show task activity in SystemView
 */
void systemview_demo_log_tasks(void);

/**
 * @brief Mark critical application events for SystemView timeline
 */
void systemview_mark_event(const char* event_name);

#ifdef __cplusplus
}
#endif

#endif // SYSTEMVIEW_DEMO_H