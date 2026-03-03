/**
 * @name TOCTOU race condition on shared state
 * @description A check-then-act pattern on a shared variable without
 *              holding a lock. The variable could be modified by another
 *              task between the check and the use (TOCTOU).
 * @kind problem
 * @problem.severity error
 * @precision medium
 * @id rtos/race-condition-toctou
 * @tags correctness
 *       concurrency
 *       rtos
 *       race-condition
 */

import cpp
import lib.FreeRTOS

/**
 * A global or file-static variable that is checked in a condition
 * and then used or modified shortly after.
 */
class ToctouCandidate extends VariableAccess {
  IfStmt ifStmt;

  ToctouCandidate() {
    // The variable access is in the condition of an if statement
    this = ifStmt.getCondition().getAChild*() and
    // The variable is global or static (shared between tasks)
    (this.getTarget() instanceof GlobalVariable or this.getTarget().isStatic()) and
    not this.getTarget().isConst() and
    not this.getTarget().getType().isConst()
  }

  /** Get the if statement containing this check. */
  IfStmt getIfStmt() { result = ifStmt }
}

/**
 * Holds if the same variable checked in the condition is used again
 * in the then-branch without a mutex protecting the check+use pair.
 */
predicate isToctouPattern(ToctouCandidate check, VariableAccess use) {
  check.getTarget() = use.getTarget() and
  // The use is in the then-branch of the if statement
  use.getEnclosingStmt().getParentStmt*() = check.getIfStmt().getThen() and
  // No mutex take before the if statement in this function
  not exists(MutexAcquire ma |
    ma.getEnclosingFunction() = check.getEnclosingFunction() and
    ma.getLocation().getStartLine() < check.getLocation().getStartLine()
  )
}

from ToctouCandidate check, VariableAccess use
where
  isToctouPattern(check, use) and
  // Exclude library code
  not check.getFile().getRelativePath().matches("%Middlewares%") and
  not check.getFile().getRelativePath().matches("%/build/%") and
  not check.getFile().getRelativePath().matches("%/Drivers/%") and
  not check.getFile().getRelativePath().matches("%freertos%")
select check,
  "TOCTOU race: variable '" + check.getTarget().getName() +
  "' is checked here and used at line " +
  use.getLocation().getStartLine().toString() +
  " without mutex protection. Another task could modify it between check and use."
