#ifndef CONFIG_H
#define CONFIG_H

// Task priorities
#define APP_TASK_PRIORITY           5
#define FEATURE_TASK_PRIORITY       4
#define EVENT_BUS_TASK_PRIORITY     6

// Task stack sizes (in bytes)
#define APP_TASK_STACK_SIZE         4096
#define FEATURE_TASK_STACK_SIZE     2048
#define EVENT_BUS_TASK_STACK_SIZE   3072

#endif // CONFIG_H
