/**
 * @name Tight loop without yield or delay
 * @description An unbounded loop body contains no calls that yield to the
 *              scheduler (delay, queue receive, semaphore take, etc.). This
 *              will starve lower-priority tasks and may trigger the task
 *              watchdog.
 * @kind problem
 * @problem.severity warning
 * @precision medium
 * @id rtos/watchdog-starvation
 * @tags correctness
 *       concurrency
 *       rtos
 *       performance
 */

import cpp
import lib.FreeRTOS

/** A while or for loop statement. */
class LoopStmt extends Stmt {
  LoopStmt() {
    this instanceof WhileStmt or
    this instanceof ForStmt or
    this instanceof DoStmt
  }

  /** Get the loop body. */
  Stmt getLoopBody() {
    this instanceof WhileStmt and result = this.(WhileStmt).getStmt()
    or
    this instanceof ForStmt and result = this.(ForStmt).getStmt()
    or
    this instanceof DoStmt and result = this.(DoStmt).getStmt()
  }

  /** Get the loop condition expression. */
  Expr getCondition() {
    this instanceof WhileStmt and result = this.(WhileStmt).getCondition()
    or
    this instanceof ForStmt and result = this.(ForStmt).getCondition()
    or
    this instanceof DoStmt and result = this.(DoStmt).getCondition()
  }
}

/**
 * Holds if the loop is unbounded — it runs indefinitely unless explicitly
 * broken out of. Patterns: while(1), while(true), for(;;), do{}while(1).
 *
 * Bounded loops (for-with-counter, while-with-decrement) are excluded
 * because they terminate naturally and don't cause starvation.
 */
predicate isUnboundedLoop(LoopStmt loop) {
  // while(1), while(true), while(!0)
  (
    loop instanceof WhileStmt and
    loop.getCondition().getValue() != "0"  // constant true
  )
  or
  // for(;;) — no condition means infinite
  (
    loop instanceof ForStmt and
    not exists(loop.(ForStmt).getCondition())
  )
  or
  // do{}while(1)
  (
    loop instanceof DoStmt and
    loop.getCondition().getValue() != "0"
  )
}

/**
 * Holds if `call` is a yielding call that appears within (or nested in)
 * the statement `s`.
 */
predicate hasYieldingCallIn(Stmt s) {
  exists(YieldingCall yc | yc.getEnclosingStmt().getParentStmt*() = s)
}

/**
 * Holds if function `f` runs before the RTOS scheduler is started,
 * so starvation is impossible.
 */
predicate isPreSchedulerFunction(Function f) {
  f.getName() = "main" or
  f.getName().matches("MX_%_Init") or
  f.getName() = "SystemClock_Config" or
  f.getName().matches("HAL_%MspInit%") or
  f.getName().matches("HAL_%MspDeInit%") or
  f.getName().matches("%_IRQHandler") or
  f.getName().matches("bl_%") or
  f.getName().matches("SEGGER_%") or
  // Init functions that set up peripherals before tasks run
  f.getName().matches("%_init") or
  f.getName().matches("%_deinit")
}

from LoopStmt loop
where
  // Only flag unbounded loops (while(1), for(;;))
  isUnboundedLoop(loop) and
  // No yielding call in the loop body
  not hasYieldingCallIn(loop.getLoopBody()) and
  // Exclude pre-scheduler / init functions
  not isPreSchedulerFunction(loop.getEnclosingFunction()) and
  // Exclude FreeRTOS/library internals
  not loop.getFile().getRelativePath().matches("%Middlewares%") and
  not loop.getFile().getRelativePath().matches("%freertos%") and
  not loop.getFile().getRelativePath().matches("%FreeRTOS%") and
  not loop.getFile().getRelativePath().matches("%/build/%") and
  not loop.getFile().getRelativePath().matches("%/Drivers/%") and
  not loop.getFile().getRelativePath().matches("%SEGGER%") and
  not loop.getFile().getRelativePath().matches("%Bootloader%")
select loop,
  "Unbounded loop in '" + loop.getEnclosingFunction().getName() +
  "' has no yield/delay call. This may starve lower-priority tasks " +
  "or trigger the task watchdog timer."
