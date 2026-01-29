/**
 * @file app_state_machine.h
 * @brief Application-level state machine
 * 
 * OPTIONAL: Use this for applications that need a central state machine
 * Delete if not needed for your project
 */

#ifndef APP_STATE_MACHINE_H
#define APP_STATE_MACHINE_H

/**
 * @brief Initialize state machine
 */
void app_state_machine_init(void);

/**
 * @brief Run state machine (call periodically)
 */
void app_state_machine_run(void);

#endif // APP_STATE_MACHINE_H
