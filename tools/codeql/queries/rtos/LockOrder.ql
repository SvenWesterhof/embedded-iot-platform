/**
 * @name Inconsistent mutex acquisition order
 * @description Two or more mutexes are acquired in different orders across
 *              functions, creating a potential deadlock (ABBA pattern).
 * @kind problem
 * @problem.severity error
 * @precision medium
 * @id rtos/lock-order
 * @tags correctness
 *       concurrency
 *       rtos
 *       deadlock
 */

import cpp
import lib.FreeRTOS

/**
 * A mutex acquisition site: the call to os_mutex_take/xSemaphoreTake
 * and the expression identifying which mutex handle is being acquired.
 */
class MutexAcquisition extends MutexAcquire {
  /** Get the mutex handle expression (first argument). */
  Expr getMutexHandle() { result = this.getArgument(0) }

  /**
   * Get a string identifying this mutex. Uses the variable name if the
   * handle is a variable access, otherwise falls back to a location string.
   */
  string getMutexId() {
    if this.getMutexHandle() instanceof VariableAccess
    then result = this.getMutexHandle().(VariableAccess).getTarget().getName()
    else result = "mutex@" + this.getLocation().getStartLine().toString()
  }
}

/**
 * Holds if function `f` acquires mutex `m1` before `m2` (by source location).
 */
predicate acquiresInOrder(Function f, MutexAcquisition acq1, MutexAcquisition acq2) {
  acq1.getEnclosingFunction() = f and
  acq2.getEnclosingFunction() = f and
  acq1.getMutexId() != acq2.getMutexId() and
  acq1.getLocation().getStartLine() < acq2.getLocation().getStartLine() and
  // Both are in the same file (intra-file analysis)
  acq1.getFile() = acq2.getFile()
}

from MutexAcquisition acqA1, MutexAcquisition acqA2,
     MutexAcquisition acqB1, MutexAcquisition acqB2,
     Function f1, Function f2
where
  // In function f1: mutex A is acquired before mutex B
  acquiresInOrder(f1, acqA1, acqA2) and
  // In function f2: mutex B is acquired before mutex A (reversed order)
  acquiresInOrder(f2, acqB1, acqB2) and
  acqA1.getMutexId() = acqB2.getMutexId() and
  acqA2.getMutexId() = acqB1.getMutexId() and
  f1 != f2 and
  // Exclude library code
  not acqA1.getFile().getRelativePath().matches("%Middlewares%") and
  not acqB1.getFile().getRelativePath().matches("%Middlewares%")
select acqA1,
  "Potential deadlock: '" + f1.getName() + "' acquires '" +
  acqA1.getMutexId() + "' then '" + acqA2.getMutexId() +
  "', but '" + f2.getName() + "' acquires them in reverse order ('" +
  acqB1.getMutexId() + "' then '" + acqB2.getMutexId() + "')."
