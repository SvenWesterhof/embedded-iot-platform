/**
 * @file app_state_machine.c
 * @brief Application-level state machine
 * 
 * OPTIONAL: Use this for applications that need a central state machine
 * Delete if not needed for your project
 */

#include "app_state_machine.h"
#include "../OS/event_bus.h"
#include "portable_log.h"

static const char *TAG = "APP_SM";

// State machine states
typedef enum {
    STATE_INIT,
    STATE_IDLE,
    STATE_RUNNING,
    STATE_ERROR,
    // Add your states here
} app_state_t;

static app_state_t current_state = STATE_INIT;

/**
 * @brief Initialize state machine
 */
void app_state_machine_init(void)
{
    LOG_I(TAG, "Initializing state machine");
    
    current_state = STATE_INIT;
    
    // Subscribe to events that trigger state transitions
    // event_bus_subscribe(EVENT_START, state_machine_event_handler);
}

/**
 * @brief Run state machine (call from main loop or task)
 */
void app_state_machine_run(void)
{
    switch (current_state) {
        case STATE_INIT:
            // TODO: Initialization logic
            // Transition to IDLE when ready
            // current_state = STATE_IDLE;
            break;
            
        case STATE_IDLE:
            // TODO: Idle state logic
            break;
            
        case STATE_RUNNING:
            // TODO: Running state logic
            break;
            
        case STATE_ERROR:
            // TODO: Error handling
            break;
            
        default:
            LOG_E(TAG, "Unknown state: %d", current_state);
            current_state = STATE_ERROR;
            break;
    }
}

/**
 * @brief Get current state
 */
app_state_t app_state_machine_get_state(void)
{
    return current_state;
}

/**
 * @brief Event handler for state transitions
 */
static void state_machine_event_handler(event_type_t event, void *data)
{
    LOG_I(TAG, "Event received: %d in state %d", event, current_state);
    
    // TODO: Handle events and perform state transitions
}
