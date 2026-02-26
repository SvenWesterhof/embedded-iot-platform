#include "event_bus.h"
#include "os_wrapper.h"
#include <string.h>
#include <stdlib.h>
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
    bool owns_data;  // true = dispatch task must free(data) after delivery
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
    os_mutex_handle_t subscriber_mutex;
    os_task_handle_t dispatch_task_handle;
    bool initialized;
} event_bus = {
    .event_queue = NULL,
    .subscriber_count = 0,
    .subscriber_mutex = NULL,
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

    // Create subscriber mutex
    event_bus.subscriber_mutex = os_mutex_create();
    if (event_bus.subscriber_mutex == NULL) {
        LOG_E(TAG, "Failed to create subscriber mutex");
        os_queue_delete(event_bus.event_queue);
        return false;
    }

    // Create dispatch task
    os_result_t result = os_task_create_pinned(
        event_dispatch_task,
        "event_dispatch",
        4096,
        NULL,
        EVENT_BUS_TASK_PRIORITY,
        &event_bus.dispatch_task_handle,
        1
    );

    if (result != OS_SUCCESS) {
        LOG_E(TAG, "Failed to create dispatch task");
        os_mutex_delete(event_bus.subscriber_mutex);
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

    if (os_mutex_take(event_bus.subscriber_mutex, 1000) != OS_SUCCESS) {
        LOG_E(TAG, "Failed to acquire subscriber mutex");
        return false;
    }

    if (event_bus.subscriber_count >= MAX_SUBSCRIBERS) {
        LOG_E(TAG, "Maximum subscribers reached");
        os_mutex_give(event_bus.subscriber_mutex);
        return false;
    }

    event_bus.subscribers[event_bus.subscriber_count].type = type;
    event_bus.subscribers[event_bus.subscriber_count].callback = callback;
    event_bus.subscriber_count++;

    os_mutex_give(event_bus.subscriber_mutex);

    LOG_I(TAG, "Subscribed to event %d (total subscribers: %d)", type, event_bus.subscriber_count);
    return true;
}

bool event_bus_unsubscribe(event_type_t type, event_callback_t callback)
{
    if (!event_bus.initialized) {
        LOG_E(TAG, "Event bus not initialized");
        return false;
    }

    if (os_mutex_take(event_bus.subscriber_mutex, 1000) != OS_SUCCESS) {
        LOG_E(TAG, "Failed to acquire subscriber mutex");
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

            os_mutex_give(event_bus.subscriber_mutex);

            LOG_I(TAG, "Unsubscribed from event %d", type);
            return true;
        }
    }

    os_mutex_give(event_bus.subscriber_mutex);

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
        .data = data,
        .owns_data = false
    };

    if (os_queue_send(event_bus.event_queue, &msg, 100) != OS_SUCCESS) {
        LOG_W(TAG, "Failed to publish event %d (queue full)", type);
        return false;
    }

    return true;
}

bool event_bus_publish_copy(event_type_t type, const void *data, size_t data_size)
{
    if (!event_bus.initialized) {
        LOG_E(TAG, "Event bus not initialized");
        return false;
    }

    void *data_copy = NULL;
    if (data != NULL && data_size > 0) {
        data_copy = malloc(data_size);
        if (data_copy == NULL) {
            LOG_E(TAG, "Failed to allocate %u bytes for event %d data", data_size, type);
            return false;
        }
        memcpy(data_copy, data, data_size);
    }

    event_message_t msg = {
        .type = type,
        .data = data_copy,
        .owns_data = (data_copy != NULL)
    };

    if (os_queue_send(event_bus.event_queue, &msg, 100) != OS_SUCCESS) {
        LOG_W(TAG, "Failed to publish event %d (queue full)", type);
        free(data_copy);
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

            if (os_mutex_take(event_bus.subscriber_mutex, 1000) == OS_SUCCESS) {
                for (uint8_t i = 0; i < event_bus.subscriber_count; i++) {
                    if (event_bus.subscribers[i].type == msg.type) {
                        event_bus.subscribers[i].callback(msg.type, msg.data);
                    }
                }
                os_mutex_give(event_bus.subscriber_mutex);
            } else {
                LOG_E(TAG, "Failed to acquire subscriber mutex for dispatch");
            }

            // Free heap-copied data after all subscribers have been called
            if (msg.owns_data && msg.data != NULL) {
                free(msg.data);
            }
        }
    }
}
