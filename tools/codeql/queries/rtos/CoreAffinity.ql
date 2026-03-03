/**
 * @name WiFi/BLE operation on wrong core or missing core pinning
 * @description A WiFi or BLE-related API is called from a task that is
 *              not pinned to Core 0 (ESP32). WiFi/BLE stack runs on Core 0
 *              and network callbacks execute there — calling WiFi APIs from
 *              Core 1 tasks can cause race conditions.
 * @kind problem
 * @problem.severity warning
 * @precision low
 * @id rtos/core-affinity
 * @tags correctness
 *       concurrency
 *       rtos
 *       esp32
 *
 * NOTE: This query is ESP32-specific and should only be run against
 * ESP32 CodeQL databases. Deferred to Phase 2 (ESP32 CI integration).
 */

import cpp
import lib.FreeRTOS

/** A call to a WiFi or BLE API that should run on Core 0. */
class WifiApiCall extends FunctionCall {
  WifiApiCall() {
    this.getTarget().getName().matches("esp_wifi_%") or
    this.getTarget().getName().matches("esp_netif_%") or
    this.getTarget().getName().matches("esp_ble_%") or
    this.getTarget().getName().matches("esp_bt_%") or
    this.getTarget().getName().matches("esp_bluedroid_%") or
    // lwIP socket/network calls
    this.getTarget().getName() in [
      "lwip_socket", "lwip_connect", "lwip_send", "lwip_recv",
      "lwip_bind", "lwip_listen", "lwip_accept", "lwip_close"
    ]
  }
}

/**
 * A task creation that pins to Core 1 (not Core 0).
 * os_task_create_pinned with core_id = 1.
 */
class Core1PinnedTask extends TaskCreation {
  Core1PinnedTask() {
    this.getTarget().getName() = "os_task_create_pinned" and
    this.getArgument(6).getValue() = "1"
  }
}

from Core1PinnedTask task, Function entry, WifiApiCall wifiCall
where
  entry = task.getEntryFunction().(FunctionAccess).getTarget() and
  entry.calls*(wifiCall.getEnclosingFunction()) and
  // Only ESP32 code
  task.getFile().getRelativePath().matches("ESP32/%")
select wifiCall,
  "WiFi/BLE API '" + wifiCall.getTarget().getName() +
  "' is reachable from task '" + entry.getName() +
  "' which is pinned to Core 1. WiFi stack runs on Core 0 — " +
  "this may cause threading issues. Pin task to Core 0 or OS_CORE_ANY."
