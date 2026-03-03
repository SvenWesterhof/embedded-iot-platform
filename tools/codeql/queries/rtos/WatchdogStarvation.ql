/**
 * @name Tight loop without yield or delay
 * @description A loop body contains no calls that yield to the scheduler
 *              (delay, queue receive, semaphore take, etc.). This will
 *              starve lower-priority tasks and may trigger the task watchdog.
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
}

/**
 * Holds if `call` is a yielding call that appears within (or nested in)
 * the statement `s`.
 */
predicate hasYieldingCallIn(Stmt s) {
  exists(YieldingCall yc | yc.getEnclosingStmt().getParentStmt*() = s)
  or
  // Also count break/return as "exiting" — not truly starvation
  exists(BreakStmt bs | bs.getParentStmt*() = s)
  or
  exists(ReturnStmt rs | rs.getParentStmt*() = s)
}

from LoopStmt loop
where
  // No yielding call in the loop body
  not hasYieldingCallIn(loop.getLoopBody()) and
  // Must be in a task function (not in initialization/main)
  exists(Function f | f = loop.getEnclosingFunction() |
    // Exclude trivial loops (less than 3 statements)
    loop.getLoopBody().(BlockStmt).getNumStmt() >= 2
    or
    not loop.getLoopBody() instanceof BlockStmt
  ) and
  // Exclude FreeRTOS/library internals
  not loop.getFile().getRelativePath().matches("%Middlewares%") and
  not loop.getFile().getRelativePath().matches("%freertos%") and
  not loop.getFile().getRelativePath().matches("%FreeRTOS%") and
  not loop.getFile().getRelativePath().matches("%/build/%") and
  // Exclude driver/HAL code (may legitimately busy-wait hardware)
  not loop.getFile().getRelativePath().matches("%/Drivers/%")
select loop,
  "Loop in '" + loop.getEnclosingFunction().getName() +
  "' has no yield/delay call. This may starve lower-priority tasks " +
  "or trigger the task watchdog timer."
