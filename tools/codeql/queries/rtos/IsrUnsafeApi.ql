/**
 * @name Non-ISR-safe API called from interrupt context
 * @description A function that is not safe to call from ISR context is
 *              reachable from an interrupt handler. This can cause
 *              deadlocks, data corruption, or undefined behavior.
 * @kind problem
 * @problem.severity error
 * @precision high
 * @id rtos/isr-unsafe-api
 * @tags correctness
 *       concurrency
 *       rtos
 *       safety
 */

import cpp
import lib.FreeRTOS
import lib.IsrContext

from NonIsrSafeCall call, Function isrRoot
where
  // The call is reachable from ISR context
  isReachableFromIsr(call.getEnclosingFunction()) and
  // Find the ISR root for the error message
  isrRoot instanceof IsrFunction and
  isrRoot.calls*(call.getEnclosingFunction()) and
  // Exclude the os_wrapper implementation (it has ISR-safe variants internally)
  not call.getFile().getBaseName().matches("os_wrapper%") and
  // Exclude FreeRTOS internals
  not call.getFile().getRelativePath().matches("%Middlewares%") and
  not call.getFile().getRelativePath().matches("%freertos%") and
  not call.getFile().getRelativePath().matches("%FreeRTOS%")
select call,
  "Non-ISR-safe API '" + call.getTarget().getName() +
  "' is reachable from ISR function '" + isrRoot.getName() +
  "'. Use the FromISR/from_isr variant instead."
