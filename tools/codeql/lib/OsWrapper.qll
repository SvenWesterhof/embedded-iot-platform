/**
 * OS Wrapper API model library.
 *
 * Maps the os_wrapper abstraction layer to its semantic properties,
 * enabling queries to reason about wrapper usage and bypass detection.
 */

import cpp

/**
 * Maps raw FreeRTOS API names to their os_wrapper equivalents.
 * Used by WrapperBypass.ql to detect direct FreeRTOS calls.
 */
predicate rawToWrapper(string rawApi, string wrapperApi) {
  rawApi = "xTaskCreate" and wrapperApi = "os_task_create" or
  rawApi = "xTaskCreatePinnedToCore" and wrapperApi = "os_task_create_pinned" or
  rawApi = "vTaskDelete" and wrapperApi = "os_task_delete" or
  rawApi = "xTaskGetCurrentTaskHandle" and wrapperApi = "os_task_get_current" or
  rawApi = "xQueueCreate" and wrapperApi = "os_queue_create" or
  rawApi = "vQueueDelete" and wrapperApi = "os_queue_delete" or
  rawApi = "xQueueSend" and wrapperApi = "os_queue_send" or
  rawApi = "xQueueSendFromISR" and wrapperApi = "os_queue_send_from_isr" or
  rawApi = "xQueueReceive" and wrapperApi = "os_queue_receive" or
  rawApi = "xQueueReceiveFromISR" and wrapperApi = "os_queue_receive_from_isr" or
  rawApi = "uxQueueMessagesWaiting" and wrapperApi = "os_queue_get_count" or
  rawApi = "xQueueReset" and wrapperApi = "os_queue_reset" or
  rawApi = "xSemaphoreCreateMutex" and wrapperApi = "os_mutex_create" or
  rawApi = "vSemaphoreDelete" and wrapperApi = "os_mutex_delete" or
  rawApi = "xSemaphoreTake" and wrapperApi = "os_mutex_take" or
  rawApi = "xSemaphoreGive" and wrapperApi = "os_mutex_give" or
  rawApi = "xSemaphoreCreateBinary" and wrapperApi = "os_semaphore_create_binary" or
  rawApi = "xSemaphoreCreateCounting" and wrapperApi = "os_semaphore_create_counting" or
  rawApi = "xSemaphoreGiveFromISR" and wrapperApi = "os_semaphore_give_from_isr" or
  rawApi = "xSemaphoreTakeFromISR" and wrapperApi = "os_semaphore_take_from_isr" or
  rawApi = "xTaskGetTickCount" and wrapperApi = "os_get_tick_count" or
  rawApi = "vTaskDelay" and wrapperApi = "os_delay_ms" or
  rawApi = "pdMS_TO_TICKS" and wrapperApi = "os_ms_to_ticks" or
  rawApi = "portYIELD_FROM_ISR" and wrapperApi = "os_yield_from_isr"
}

/** Holds if the given file is part of the os_wrapper implementation itself. */
predicate isOsWrapperFile(File f) {
  f.getBaseName().matches("os_wrapper%")
}

/**
 * The project layer hierarchy for EVENT_BUS_MISUSE detection.
 * Lower number = lower layer. Higher layers must not be called directly
 * by lower layers (should use event bus instead).
 */
int layerRank(string dirPattern) {
  dirPattern = "%/Drivers%" and result = 1 or
  dirPattern = "%/HAL_Wrapper%" and result = 1 or
  dirPattern = "%/Drivers_BSP%" and result = 1 or
  dirPattern = "%/OS/%" and result = 2 or
  dirPattern = "%/Middleware/Control/%" and result = 3 or
  dirPattern = "%/Middleware/Services/%" and result = 3 or
  dirPattern = "%/Middleware/Features/%" and result = 3 or
  dirPattern = "%/Application/%" and result = 4
}

/**
 * Get the layer rank for a file based on its path.
 * Returns -1 if file doesn't match any known layer.
 */
int getFileLayerRank(File f) {
  exists(string pattern | layerRank(pattern) = result and f.getRelativePath().matches(pattern))
  or
  not exists(string pattern, int r | layerRank(pattern) = r and f.getRelativePath().matches(pattern)) and
  result = -1
}
