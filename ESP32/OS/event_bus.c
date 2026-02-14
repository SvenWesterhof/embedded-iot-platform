#include "event_bus.h"
#include "os_wrapper.h"
#include <string.h>
#include "portable_log.h"

static const char *TAG = "EVENT_BUS";

#define MAX_SUBSCRIBERS 16
#define EVENT_QUEUE_SIZE 20
#define EVENT_BUS_TASK_PRIORITY 6
#define EVENT_BUS_TASK_STACK 3072

// Event message structure
typedef struct {
    event_type_t type;
    void *data;
} event_message_t;

// Subscriber entry
typedef struct {
    event_type_t type;
    event_callback_t callback;
} subscriber_t;

// Event bus context
static struct {
    os_queue_handle_t event_queue;
    subscriber_t subscribers[MAX_SUBSCRIBERS];
    uint8_t subscriber_count;
    os_task_handle_t dispatch_task_handle;
    bool initialized;
} event_bus = {
    .event_queue = NULL,
    .subscriber_count = 0,
    .dispatch_task_handle = NULL,
    .initialized = false
};

// Forward declarations
static void event_dispatch_task(void *pvParameters);

bool event_bus_init(void)
{
    if (event_bus.initialized) {
        LOG_W(TAG, "Event bus already initialized");
        return true;
    }
    
    LOG_I(TAG, "Initializing event bus");
    
    // Create event queue
    event_bus.event_queue = os_queue_create(EVENT_QUEUE_SIZE, sizeof(event_message_t));
    if (event_bus.event_queue == NULL) {
        LOG_E(TAG, "Failed to create event queue");
        return false;
    }
    
    // Create dispatch task
    os_result_t result = os_task_create_pinned(
        event_dispatch_task,
        "event_dispatch",
        3072,
        NULL,
        EVENT_BUS_TASK_PRIORITY,
        &event_bus.dispatch_task_handle,
        1
    );
    
    if (result != OS_SUCCESS) {
        LOG_E(TAG, "Failed to create dispatch task");
        os_queue_delete(event_bus.event_queue);
        return false;
    }
    
    event_bus.subscriber_count = 0;
    event_bus.initialized = true;
    
    LOG_I(TAG, "Event bus initialized");
    return true;
}

bool event_bus_subscribe(event_type_t type, event_callback_t callback)
{
    if (!event_bus.initialized) {
        LOG_E(TAG, "Event bus not initialized");
        return false;
    }
    
    if (callback == NULL) {
        LOG_E(TAG, "Invalid callback");
        return false;
    }
    
    if (event_bus.subscriber_count >= MAX_SUBSCRIBERS) {
        LOG_E(TAG, "Maximum subscribers reached");
        return false;
    }
    
    event_bus.subscribers[event_bus.subscriber_count].type = type;
    event_bus.subscribers[event_bus.subscriber_count].callback = callback;
    event_bus.subscriber_count++;
    
    LOG_I(TAG, "Subscribed to event %d (total subscribers: %d)", type, event_bus.subscriber_count);
    return true;
}

bool event_bus_unsubscribe(event_type_t type, event_callback_t callback)
{
    if (!event_bus.initialized) {
        LOG_E(TAG, "Event bus not initialized");
        return false;
    }
    
    for (uint8_t i = 0; i < event_bus.subscriber_count; i++) {
        if (event_bus.subscribers[i].type == type && 
            event_bus.subscribers[i].callback == callback) {
            
            // Remove subscriber by shifting array
            for (uint8_t j = i; j < event_bus.subscriber_count - 1; j++) {
                event_bus.subscribers[j] = event_bus.subscribers[j + 1];
            }
            event_bus.subscriber_count--;
            
            LOG_I(TAG, "Unsubscribed from event %d", type);
            return true;
        }
    }
    
    LOG_W(TAG, "Subscriber not found");
    return false;
}

bool event_bus_publish(event_type_t type, void *data)
{
    if (!event_bus.initialized) {
        LOG_E(TAG, "Event bus not initialized");
        return false;
    }
    
    event_message_t msg = {
        .type = type,
        .data = data
    };
    
    if (os_queue_send(event_bus.event_queue, &msg, 100) != OS_SUCCESS) {
        LOG_W(TAG, "Failed to publish event %d (queue full)", type);
        return false;
    }
    
    return true;
}

static void event_dispatch_task(void *pvParameters)
{
    event_message_t msg;
    
    LOG_I(TAG, "Event dispatch task started");
    
    while (1) {
        if (os_queue_receive(event_bus.event_queue, &msg, OS_WAIT_FOREVER) == OS_SUCCESS) {
            LOG_D(TAG, "Dispatching event %d", msg.type);
            
            // Call all subscribers for this event type
            for (uint8_t i = 0; i < event_bus.subscriber_count; i++) {
                if (event_bus.subscribers[i].type == msg.type) {
                    event_bus.subscribers[i].callback(msg.type, msg.data);
                }
            }
        }
    }
}
