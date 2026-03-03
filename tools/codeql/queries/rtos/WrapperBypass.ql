/**
 * @name Direct FreeRTOS API call bypassing os_wrapper
 * @description Application code calls raw FreeRTOS APIs instead of the
 *              os_wrapper abstraction. This breaks portability between
 *              ESP32 and STM32 targets.
 * @kind problem
 * @problem.severity warning
 * @precision high
 * @id rtos/wrapper-bypass
 * @tags correctness
 *       portability
 *       rtos
 */

import cpp
import lib.FreeRTOS
import lib.OsWrapper

from RawFreeRtosCall call, string rawApi, string wrapperApi
where
  rawApi = call.getTarget().getName() and
  rawToWrapper(rawApi, wrapperApi) and
  // Exclude the os_wrapper implementation itself
  not isOsWrapperFile(call.getFile()) and
  // Exclude FreeRTOS library internals
  not call.getFile().getRelativePath().matches("%Middlewares%") and
  not call.getFile().getRelativePath().matches("%/build/%") and
  // Exclude FreeRTOS port/kernel source
  not call.getFile().getRelativePath().matches("%freertos%") and
  not call.getFile().getRelativePath().matches("%FreeRTOS%")
select call,
  "Direct FreeRTOS API '" + rawApi + "' bypasses os_wrapper. Use '" +
  wrapperApi + "' instead."
