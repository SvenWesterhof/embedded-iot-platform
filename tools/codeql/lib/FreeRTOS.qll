/**
 * FreeRTOS API model library for RTOS safety analysis.
 *
 * Classifies raw FreeRTOS functions by behavior: blocking, ISR-safe,
 * critical-section entry/exit, and allocation.
 */

import cpp

/** A call to a raw FreeRTOS API (not through os_wrapper). */
class RawFreeRtosCall extends FunctionCall {
  RawFreeRtosCall() {
    this.getTarget().getName() in [
      // Task operations
      "xTaskCreate", "xTaskCreatePinnedToCore", "vTaskDelete",
      "xTaskGetCurrentTaskHandle",
      // Queue operations
      "xQueueCreate", "vQueueDelete", "xQueueSend", "xQueueSendFromISR",
      "xQueueReceive", "xQueueReceiveFromISR", "uxQueueMessagesWaiting",
      "xQueueReset",
      // Mutex operations
      "xSemaphoreCreateMutex", "vSemaphoreDelete",
      "xSemaphoreTake", "xSemaphoreGive",
      // Semaphore operations
      "xSemaphoreCreateBinary", "xSemaphoreCreateCounting",
      "xSemaphoreGiveFromISR", "xSemaphoreTakeFromISR",
      // Time operations
      "xTaskGetTickCount", "vTaskDelay", "pdMS_TO_TICKS",
      // ISR utilities
      "portYIELD_FROM_ISR"
    ]
  }
}

/** A call to a FreeRTOS or os_wrapper function that may block the calling task. */
class BlockingCall extends FunctionCall {
  BlockingCall() {
    this.getTarget().getName() in [
      // os_wrapper blocking calls
      "os_queue_send", "os_queue_receive",
      "os_mutex_take",
      "os_semaphore_take",
      "os_delay_ms",
      // Raw FreeRTOS blocking calls
      "xQueueSend", "xQueueSendToBack", "xQueueSendToFront",
      "xQueueReceive", "xQueuePeek",
      "xSemaphoreTake",
      "vTaskDelay", "vTaskDelayUntil",
      "ulTaskNotifyTake", "xTaskNotifyWait"
    ]
  }
}

/** A call to a FreeRTOS function that is NOT safe to call from ISR context. */
class NonIsrSafeCall extends FunctionCall {
  NonIsrSafeCall() {
    this.getTarget().getName() in [
      // os_wrapper non-ISR-safe
      "os_queue_send", "os_queue_receive",
      "os_mutex_take", "os_mutex_give",
      "os_mutex_create", "os_mutex_delete",
      "os_semaphore_take", "os_semaphore_give",
      "os_semaphore_create_binary", "os_semaphore_create_counting",
      "os_delay_ms",
      // Raw FreeRTOS non-ISR-safe
      "xQueueSend", "xQueueReceive",
      "xSemaphoreTake", "xSemaphoreGive",
      "xSemaphoreCreateMutex", "xSemaphoreCreateBinary",
      "vTaskDelay", "vTaskSuspend",
      "vSemaphoreDelete"
    ]
  }
}

/** A call that yields or delays the current task (prevents watchdog starvation). */
class YieldingCall extends FunctionCall {
  YieldingCall() {
    this.getTarget().getName() in [
      "os_delay_ms", "vTaskDelay", "vTaskDelayUntil",
      "os_queue_receive", "xQueueReceive",
      "os_semaphore_take", "xSemaphoreTake",
      "os_mutex_take", "xSemaphoreTake",
      "taskYIELD", "portYIELD",
      "ulTaskNotifyTake", "xTaskNotifyWait"
    ]
  }
}

/** A call that enters a FreeRTOS critical section (disables interrupts). */
class CriticalSectionEntry extends FunctionCall {
  CriticalSectionEntry() {
    this.getTarget().getName() in [
      "taskENTER_CRITICAL", "portENTER_CRITICAL",
      "taskENTER_CRITICAL_FROM_ISR", "portENTER_CRITICAL_FROM_ISR",
      "vPortEnterCritical"
    ]
  }
}

/** A call that exits a FreeRTOS critical section (re-enables interrupts). */
class CriticalSectionExit extends FunctionCall {
  CriticalSectionExit() {
    this.getTarget().getName() in [
      "taskEXIT_CRITICAL", "portEXIT_CRITICAL",
      "taskEXIT_CRITICAL_FROM_ISR", "portEXIT_CRITICAL_FROM_ISR",
      "vPortExitCritical"
    ]
  }
}

/** A call that acquires a mutex (os_wrapper or raw FreeRTOS). */
class MutexAcquire extends FunctionCall {
  MutexAcquire() {
    this.getTarget().getName() in [
      "os_mutex_take", "xSemaphoreTake"
    ]
  }
}

/** A call that releases a mutex (os_wrapper or raw FreeRTOS). */
class MutexRelease extends FunctionCall {
  MutexRelease() {
    this.getTarget().getName() in [
      "os_mutex_give", "xSemaphoreGive"
    ]
  }
}

/** A heap allocation call (malloc, calloc, pvPortMalloc). */
class HeapAllocation extends FunctionCall {
  HeapAllocation() {
    this.getTarget().getName() in [
      "malloc", "calloc", "realloc", "pvPortMalloc"
    ]
  }
}

/** A heap deallocation call (free, vPortFree). */
class HeapDeallocation extends FunctionCall {
  HeapDeallocation() {
    this.getTarget().getName() in [
      "free", "vPortFree"
    ]
  }
}

/** A call that creates an RTOS task. */
class TaskCreation extends FunctionCall {
  TaskCreation() {
    this.getTarget().getName() in [
      "os_task_create", "os_task_create_pinned",
      "xTaskCreate", "xTaskCreatePinnedToCore",
      "xTaskCreateStatic"
    ]
  }

  /** Get the task entry function argument (first argument). */
  Expr getEntryFunction() { result = this.getArgument(0) }

  /** Get the stack size argument (third argument). */
  Expr getStackSize() { result = this.getArgument(2) }

  /** Get the priority argument (fifth argument for os_wrapper, fourth for raw). */
  Expr getPriority() {
    if this.getTarget().getName().matches("os_%")
    then result = this.getArgument(4)
    else result = this.getArgument(3)
  }
}

/** A call that creates a mutex. */
class MutexCreation extends FunctionCall {
  MutexCreation() {
    this.getTarget().getName() in [
      "os_mutex_create", "xSemaphoreCreateMutex"
    ]
  }
}
