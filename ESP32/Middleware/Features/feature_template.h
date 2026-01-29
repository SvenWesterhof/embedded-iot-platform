/**
 * @file feature_template.h
 * @brief Template for creating new features in the Middleware layer
 * 
 * Copy this file and rename to feat_<your_feature>.h
 */

#ifndef FEATURE_TEMPLATE_H
#define FEATURE_TEMPLATE_H

#include <stdbool.h>

/**
 * @brief Initialize the feature
 * @return true on success, false on failure
 */
bool feature_template_init(void);

/**
 * @brief Start the feature (create tasks)
 * @return true on success, false on failure
 */
bool feature_template_start(void);

/**
 * @brief Stop the feature
 */
void feature_template_stop(void);

/**
 * @brief Example public API function
 * @return true on success, false on failure
 */
bool feature_template_do_something(void);

#endif // FEATURE_TEMPLATE_H
