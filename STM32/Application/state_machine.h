#ifndef STATE_MACHINE_H
#define STATE_MACHINE_H

#include <stdint.h>

/**
 * @file state_machine.h
 * @brief Application state machine for business logic
 * 
 * This module manages the main application states and transitions.
 */

// Application States
typedef enum {
    APP_STATE_INIT,
    APP_STATE_IDLE,
    APP_STATE_RUNNING,
    APP_STATE_ERROR,
    APP_STATE_SLEEP
} app_state_t;

// State Machine Functions
void state_machine_init(void);
void state_machine_run(void);
app_state_t state_machine_get_state(void);
void state_machine_set_state(app_state_t new_state);

#endif // STATE_MACHINE_H
