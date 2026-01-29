#include "app_main.h"
#include "services.h"
#include "event_bus.h"
#include "portable_log.h"

static const char *TAG = "APP";

void app_init(void)
{
    //LOG_I(TAG, "Application initializing...");

    // Initialize event bus first
    event_bus_init();
    LOG_I(TAG, "Event bus initialized");

    // Initialize services (including display service which subscribes to events)
    services_init();
    LOG_I(TAG, "Services initialized");

    LOG_I(TAG, "Application initialized successfully");
}

void app_run(void)
{
    
    // Run services (they publish events)
    services_run();
    // Process any pending events
    event_bus_process();
    
}
