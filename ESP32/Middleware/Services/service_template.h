/**
 * @file service_template.h
 * @brief Template for creating shared services in the Middleware layer
 * 
 * Services are stateless utility functions shared by multiple features
 * Copy this file and rename to svc_<your_service>.h
 */

#ifndef SERVICE_TEMPLATE_H
#define SERVICE_TEMPLATE_H

#include <stdbool.h>

/**
 * @brief Initialize service (if needed)
 * @return true on success, false on failure
 */
bool service_template_init(void);

/**
 * @brief Example service function
 * @param input Input data
 * @param output Output data
 * @return true on success, false on failure
 */
bool service_template_process(void *input, void *output);

#endif // SERVICE_TEMPLATE_H
