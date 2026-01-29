#ifndef OS_CONFIG_H
#define OS_CONFIG_H

// FreeRTOS Task Priorities
#define EVENT_BUS_TASK_PRIORITY     6
#define APP_MAIN_TASK_PRIORITY      5
#define FEATURE_TASK_PRIORITY       4
#define IDLE_TASK_PRIORITY          1

// Task Stack Sizes (in words, not bytes)
#define EVENT_BUS_TASK_STACK        3072
#define APP_MAIN_TASK_STACK         4096
#define FEATURE_TASK_STACK          2048

// Queue Sizes
#define EVENT_QUEUE_SIZE            20
#define DATA_QUEUE_SIZE             10

#endif // OS_CONFIG_H
