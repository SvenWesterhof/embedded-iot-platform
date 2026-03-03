/**
 * @name Blocking call inside critical section
 * @description A blocking call (mutex take, queue receive, delay) is made
 *              between taskENTER_CRITICAL and taskEXIT_CRITICAL. This will
 *              deadlock because the scheduler is suspended in a critical section.
 * @kind problem
 * @problem.severity error
 * @precision high
 * @id rtos/blocking-in-critical
 * @tags correctness
 *       concurrency
 *       rtos
 *       deadlock
 */

import cpp
import lib.FreeRTOS

/**
 * Holds if `enter` and `exit` are a critical section pair in the same
 * function, and `blocking` is a blocking call between them (by source location).
 */
predicate blockingBetweenCriticalPair(
  CriticalSectionEntry enter, CriticalSectionExit exit_, BlockingCall blocking
) {
  enter.getEnclosingFunction() = exit_.getEnclosingFunction() and
  enter.getEnclosingFunction() = blocking.getEnclosingFunction() and
  // All in the same file
  enter.getFile() = exit_.getFile() and
  enter.getFile() = blocking.getFile() and
  // enter < blocking < exit by line number
  enter.getLocation().getStartLine() < blocking.getLocation().getStartLine() and
  blocking.getLocation().getStartLine() < exit_.getLocation().getStartLine()
}

from CriticalSectionEntry enter, CriticalSectionExit exit_, BlockingCall blocking
where
  blockingBetweenCriticalPair(enter, exit_, blocking) and
  // Exclude FreeRTOS internals
  not blocking.getFile().getRelativePath().matches("%Middlewares%") and
  not blocking.getFile().getRelativePath().matches("%freertos%") and
  not blocking.getFile().getRelativePath().matches("%FreeRTOS%")
select blocking,
  "Blocking call '" + blocking.getTarget().getName() +
  "' inside critical section (entered at line " +
  enter.getLocation().getStartLine().toString() +
  "). This will deadlock because the scheduler is suspended."
