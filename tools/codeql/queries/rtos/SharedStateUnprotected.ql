/**
 * @name Unprotected access to shared state
 * @description A global or static variable is accessed (read or written)
 *              from multiple functions without mutex protection, creating
 *              a potential data race between RTOS tasks.
 * @kind problem
 * @problem.severity error
 * @precision medium
 * @id rtos/shared-state-unprotected
 * @tags correctness
 *       concurrency
 *       rtos
 *       race-condition
 */

import cpp
import lib.FreeRTOS

/** A global or file-static variable that could be shared between tasks. */
class SharedVariable extends Variable {
  SharedVariable() {
    (this instanceof GlobalVariable or this.isStatic()) and
    not this.isConst() and
    // Exclude string literals and constants
    not this.getType().isConst() and
    // Must be in application code — exclude vendor/generated/library code
    not this.getFile().getRelativePath().matches("%Middlewares%") and
    not this.getFile().getRelativePath().matches("%/build/%") and
    not this.getFile().getRelativePath().matches("%freertos%") and
    not this.getFile().getRelativePath().matches("%FreeRTOS%") and
    not this.getFile().getRelativePath().matches("%/Drivers/%") and
    not this.getFile().getRelativePath().matches("%SEGGER%") and
    not this.getFile().getRelativePath().matches("%Bootloader%") and
    // Exclude CubeMX-generated peripheral init code
    not this.getFile().getRelativePath().matches("%Core/Src/stm32%") and
    not this.getFile().getRelativePath().matches("%Core/Src/system_%")
  }
}

/**
 * Holds if the variable access `va` is NOT protected by a mutex
 * (no os_mutex_take/xSemaphoreTake call precedes it in the same function
 * without a corresponding os_mutex_give/xSemaphoreGive before it).
 *
 * Simplified heuristic: checks if there is ANY mutex take in the same
 * function before this access. A more precise analysis would track
 * specific mutex handles.
 */
predicate isUnprotectedAccess(VariableAccess va) {
  not exists(MutexAcquire ma |
    ma.getEnclosingFunction() = va.getEnclosingFunction() and
    ma.getLocation().getStartLine() < va.getLocation().getStartLine()
  )
}

/**
 * Holds if variable `v` is written in one function and read or written
 * in another, where at least one access is unprotected.
 */
predicate hasUnprotectedCrossTaskAccess(SharedVariable v, VariableAccess unprotected) {
  // Variable is accessed from at least 2 different functions
  exists(Function f1, Function f2 |
    f1 != f2 and
    exists(VariableAccess va1 | va1.getTarget() = v and va1.getEnclosingFunction() = f1) and
    exists(VariableAccess va2 | va2.getTarget() = v and va2.getEnclosingFunction() = f2)
  ) and
  // At least one access is a write
  exists(VariableAccess write |
    write.getTarget() = v and
    write.isUsedAsLValue()
  ) and
  // The flagged access is unprotected
  unprotected.getTarget() = v and
  isUnprotectedAccess(unprotected)
}

/**
 * Holds if function `f` is a CubeMX-generated init or an IRQ handler that
 * runs before the RTOS scheduler, making mutex protection unnecessary.
 */
predicate isPreSchedulerOrVendorFunction(Function f) {
  f.getName().matches("MX_%_Init") or
  f.getName() = "SystemClock_Config" or
  f.getName() = "main" or
  f.getName().matches("HAL_%MspInit%") or
  f.getName().matches("HAL_%MspDeInit%") or
  // IRQ handlers access peripheral handles directly — this is expected
  f.getName().matches("%_IRQHandler") or
  // Bootloader functions
  f.getName().matches("bl_%") or
  // SEGGER RTT internals
  f.getName().matches("SEGGER_%") or
  f.getName().matches("_DoInit%") or
  f.getName().matches("_PostTerminal%")
}

from SharedVariable v, VariableAccess unprotected
where
  hasUnprotectedCrossTaskAccess(v, unprotected) and
  // Exclude vendor/generated/library code at the access site
  not unprotected.getFile().getRelativePath().matches("%Middlewares%") and
  not unprotected.getFile().getRelativePath().matches("%/Drivers/%") and
  not unprotected.getFile().getRelativePath().matches("%SEGGER%") and
  not unprotected.getFile().getRelativePath().matches("%Bootloader%") and
  not unprotected.getFile().getRelativePath().matches("%Core/Src/stm32%") and
  not unprotected.getFile().getRelativePath().matches("%Core/Src/system_%") and
  // Exclude pre-scheduler init functions and IRQ handlers
  not isPreSchedulerOrVendorFunction(unprotected.getEnclosingFunction()) and
  // Exclude variables that are only HAL peripheral handles (huart*, hspi*, hi2c*, hrtc*, htim*)
  not v.getName().matches("huart%") and
  not v.getName().matches("hspi%") and
  not v.getName().matches("hi2c%") and
  not v.getName().matches("hrtc%") and
  not v.getName().matches("htim%") and
  not v.getName().matches("hdma_%") and
  not v.getName().matches("SystemCoreClock") and
  not v.getName().matches("uwTick%")
select unprotected,
  "Unprotected access to shared variable '" + v.getName() +
  "' in function '" + unprotected.getEnclosingFunction().getName() +
  "'. This variable is also accessed from other functions — potential data race."
