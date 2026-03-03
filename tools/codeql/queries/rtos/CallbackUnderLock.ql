/**
 * @name Callback invocation under mutex lock
 * @description An indirect function call (via function pointer) occurs
 *              between os_mutex_take and os_mutex_give. This can cause
 *              unbounded lock hold time, priority inversion, or deadlock
 *              if the callback re-enters the lock or blocks.
 * @kind problem
 * @problem.severity warning
 * @precision medium
 * @id rtos/callback-under-lock
 * @tags correctness
 *       concurrency
 *       rtos
 */

import cpp
import lib.FreeRTOS

/**
 * Holds if there is a mutex take at `acquire`, a mutex give at `release`,
 * and an indirect call (ExprCall) `callback` between them in the same function.
 */
predicate callbackBetweenMutexPair(
  MutexAcquire acquire, MutexRelease release, ExprCall callback
) {
  acquire.getEnclosingFunction() = release.getEnclosingFunction() and
  acquire.getEnclosingFunction() = callback.getEnclosingFunction() and
  acquire.getFile() = release.getFile() and
  acquire.getFile() = callback.getFile() and
  // acquire < callback < release by line number
  acquire.getLocation().getStartLine() < callback.getLocation().getStartLine() and
  callback.getLocation().getStartLine() < release.getLocation().getStartLine()
}

from MutexAcquire acquire, MutexRelease release, ExprCall callback
where
  callbackBetweenMutexPair(acquire, release, callback) and
  // Exclude library code
  not callback.getFile().getRelativePath().matches("%Middlewares%") and
  not callback.getFile().getRelativePath().matches("%/build/%") and
  not callback.getFile().getRelativePath().matches("%freertos%")
select callback,
  "Indirect call (callback/function pointer) invoked while holding mutex " +
  "(acquired at line " + acquire.getLocation().getStartLine().toString() +
  ", released at line " + release.getLocation().getStartLine().toString() +
  "). Callback may cause unbounded lock hold time or deadlock."
