#ifndef SERV_DISPLAY_H
#define SERV_DISPLAY_H

/**
 * @file serv_display.h
 * @brief Display Service - Manages display output
 * 
 * This service subscribes to relevant events and updates the display accordingly.
 * It decouples display logic from other services.
 */

void display_init(void);
void display_run(void);

#endif // SERV_DISPLAY_H
