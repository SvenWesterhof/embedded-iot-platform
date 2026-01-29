#include "state_machine.h"
#include "config.h"

// Private variables
static app_state_t current_state = APP_STATE_INIT;
static app_state_t previous_state = APP_STATE_INIT;

/**
 * @brief Initialize the state machine
 */
void state_machine_init(void)
{
    current_state = APP_STATE_INIT;
    previous_state = APP_STATE_INIT;
}

/**
 * @brief Run the state machine (called periodically)
 */
void state_machine_run(void)
{
    switch (current_state)
    {
        case APP_STATE_INIT:
            // Initialization logic
            // Transition to IDLE after init complete
            current_state = APP_STATE_IDLE;
            break;
            
        case APP_STATE_IDLE:
            // Idle state logic
            // Wait for events or conditions to start running
            break;
            
        case APP_STATE_RUNNING:
            // Main running state logic
            break;
            
        case APP_STATE_ERROR:
            // Error handling logic
            break;
            
        case APP_STATE_SLEEP:
            // Low-power sleep state logic
            break;
            
        default:
            // Invalid state - go to error
            current_state = APP_STATE_ERROR;
            break;
    }
}

/**
 * @brief Get current state
 * @return Current application state
 */
app_state_t state_machine_get_state(void)
{
    return current_state;
}

/**
 * @brief Set new state
 * @param new_state New application state to transition to
 */
void state_machine_set_state(app_state_t new_state)
{
    previous_state = current_state;
    current_state = new_state;
}
