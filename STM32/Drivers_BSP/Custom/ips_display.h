#ifndef IPS_DISPLAY_H
#define IPS_DISPLAY_H

#include "hal_spi.h"

typedef enum {
    IPS_DISPLAY_OK = 0,
    IPS_DISPLAY_ERROR
} ips_display_status_t;

// Initialize driver only (no hardware setup)
ips_display_status_t ips_display_init();

// Open connection and configure display (power on, initialize, draw UI)
ips_display_status_t ips_display_open();

// Close connection and power down display
ips_display_status_t ips_display_close();

// Deinitialize driver
void ips_display_deinit();

ips_display_status_t ips_display_write_temp_data(float temperature, float humidity);


#endif // IPS_DISPLAY_H
