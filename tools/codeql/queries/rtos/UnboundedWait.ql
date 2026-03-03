/**
 * @name Unbounded wait on mutex or unchecked wait return
 * @description A mutex take with OS_WAIT_FOREVER (unbounded), or any
 *              blocking wait whose return value is not checked. Unbounded
 *              mutex waits can cause indefinite task stalls; unchecked
 *              returns hide timeout failures.
 * @kind problem
 * @problem.severity warning
 * @precision high
 * @id rtos/unbounded-wait
 * @tags correctness
 *       concurrency
 *       rtos
 */

import cpp

/**
 * A call to os_mutex_take or xSemaphoreTake with OS_WAIT_FOREVER (0xFFFFFFFF).
 */
class UnboundedMutexWait extends FunctionCall {
  UnboundedMutexWait() {
    this.getTarget().getName() in ["os_mutex_take", "xSemaphoreTake"] and
    exists(Expr timeoutArg |
      // Second argument for os_mutex_take, third for xSemaphoreTake
      (
        this.getTarget().getName() = "os_mutex_take" and timeoutArg = this.getArgument(1)
        or
        this.getTarget().getName() = "xSemaphoreTake" and timeoutArg = this.getArgument(1)
      ) and
      // Check for OS_WAIT_FOREVER (0xFFFFFFFF) or portMAX_DELAY
      // The value is resolved after macro expansion, so check the numeric value
      timeoutArg.getValue() = "4294967295" // 0xFFFFFFFF (OS_WAIT_FOREVER / portMAX_DELAY)
    )
  }
}

/**
 * A blocking wait call whose return value is discarded (not used in
 * a condition or assignment).
 */
class UncheckedWait extends FunctionCall {
  UncheckedWait() {
    this.getTarget().getName() in [
      "os_mutex_take", "os_queue_send", "os_queue_receive",
      "os_semaphore_take"
    ] and
    // Return value is not used
    exists(ExprStmt es | es.getExpr() = this)
  }
}

from FunctionCall wait, string issue
where
  (
    wait instanceof UnboundedMutexWait and
    issue = "OS_WAIT_FOREVER on mutex '" + wait.getTarget().getName() +
            "' — task may stall indefinitely if mutex holder is blocked or deleted"
    or
    wait instanceof UncheckedWait and
    not wait instanceof UnboundedMutexWait and
    issue = "Return value of '" + wait.getTarget().getName() +
            "' is not checked — timeout or error will be silently ignored"
  ) and
  not wait.getFile().getRelativePath().matches("%Middlewares%") and
  not wait.getFile().getRelativePath().matches("%/build/%") and
  not wait.getFile().getRelativePath().matches("%freertos%") and
  not wait.getFile().getRelativePath().matches("%os_wrapper%")
select wait, issue
