/**
 * @name Core affinity misuse
 * @description Detects two patterns:
 *   1. (STM32) os_task_create_pinned() used on a single-core platform —
 *      core pinning is meaningless and may silently fail.
 *   2. (ESP32) WiFi/BLE API called from a task pinned to Core 1 —
 *      WiFi stack runs on Core 0 and this may cause threading issues.
 * @kind problem
 * @problem.severity warning
 * @precision medium
 * @id rtos/core-affinity
 * @tags correctness
 *       concurrency
 *       rtos
 */

import cpp
import lib.FreeRTOS

// =========================================================================
// Pattern 1: os_task_create_pinned on STM32 (single-core ARM Cortex-M)
// =========================================================================

/**
 * Any call to os_task_create_pinned or xTaskCreatePinnedToCore in STM32 code.
 * STM32F4/F7/H7 are single-core — core affinity is an ESP32-specific feature.
 */
class PinnedTaskOnSingleCore extends TaskCreation {
  PinnedTaskOnSingleCore() {
    this.getTarget().getName() in ["os_task_create_pinned", "xTaskCreatePinnedToCore"] and
    // In STM32 code (not ESP32)
    this.getFile().getRelativePath().matches("STM32/%")
  }
}

// =========================================================================
// Pattern 2: WiFi API from Core 1 task (ESP32-specific)
// =========================================================================

/** A call to a WiFi or BLE API that should run on Core 0. */
class WifiApiCall extends FunctionCall {
  WifiApiCall() {
    this.getTarget().getName().matches("esp_wifi_%") or
    this.getTarget().getName().matches("esp_netif_%") or
    this.getTarget().getName().matches("esp_ble_%") or
    this.getTarget().getName().matches("esp_bt_%") or
    this.getTarget().getName().matches("esp_bluedroid_%") or
    this.getTarget().getName() in [
      "lwip_socket", "lwip_connect", "lwip_send", "lwip_recv",
      "lwip_bind", "lwip_listen", "lwip_accept", "lwip_close"
    ]
  }
}

/**
 * A task creation that pins to Core 1 (not Core 0).
 */
class Core1PinnedTask extends TaskCreation {
  Core1PinnedTask() {
    this.getTarget().getName() = "os_task_create_pinned" and
    this.getArgument(6).getValue() = "1"
  }
}

from FunctionCall call, string message
where
  (
    // Pattern 1: STM32 pinned task
    call instanceof PinnedTaskOnSingleCore and
    exists(Expr entry | entry = call.(TaskCreation).getEntryFunction() |
      message = "os_task_create_pinned() used for task '" +
        entry.toString() +
        "' on STM32 single-core platform. Core pinning is an ESP32 feature — " +
        "use os_task_create() instead."
    )
    or
    // Pattern 2: ESP32 WiFi from Core 1
    call instanceof WifiApiCall and
    exists(Core1PinnedTask task, Function entry |
      entry = task.getEntryFunction().(FunctionAccess).getTarget() and
      entry.calls*(call.getEnclosingFunction()) and
      task.getFile().getRelativePath().matches("ESP32/%") and
      message = "WiFi/BLE API '" + call.getTarget().getName() +
        "' is reachable from task '" + entry.getName() +
        "' which is pinned to Core 1. WiFi stack runs on Core 0 — " +
        "pin task to Core 0 or OS_CORE_ANY."
    )
  ) and
  not call.getFile().getRelativePath().matches("%Middlewares%") and
  not call.getFile().getRelativePath().matches("%/build/%") and
  not call.getFile().getRelativePath().matches("%freertos%")
select call, message
