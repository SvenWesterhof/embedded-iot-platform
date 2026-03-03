/**
 * @name Priority inversion — shared mutex between tasks with large priority gap
 * @description A mutex is acquired by both a high-priority and a low-priority
 *              task (priority difference >= 3). Without priority inheritance,
 *              a medium-priority task can preempt the low-priority task while
 *              it holds the mutex, causing the high-priority task to block.
 * @kind problem
 * @problem.severity error
 * @precision medium
 * @id rtos/priority-inversion
 * @tags correctness
 *       concurrency
 *       rtos
 */

import cpp
import lib.FreeRTOS

/**
 * A task creation site with its entry function and priority.
 */
class TaskWithPriority extends TaskCreation {
  /** Get the task entry function (resolved through function pointers). */
  Function getResolvedEntryFunction() {
    result = this.getEntryFunction().(FunctionAccess).getTarget()
  }

  /** Get the numeric priority value (if it's a compile-time constant). */
  int getPriorityValue() {
    result = this.getPriority().getValue().toInt()
  }
}

/**
 * Holds if function `f` is transitively reachable from the task entry
 * function `entry`.
 */
predicate isReachableFromTask(Function entry, Function f) {
  f = entry
  or
  exists(Function mid | isReachableFromTask(entry, mid) and mid.calls(f))
}

/**
 * Holds if a mutex with handle variable name `mutexId` is acquired by
 * a function reachable from task with entry `taskEntry`.
 */
predicate taskAcquiresMutex(Function taskEntry, string mutexId, MutexAcquire acq) {
  isReachableFromTask(taskEntry, acq.getEnclosingFunction()) and
  acq.getArgument(0) instanceof VariableAccess and
  mutexId = acq.getArgument(0).(VariableAccess).getTarget().getName()
}

from TaskWithPriority highTask, TaskWithPriority lowTask,
     string mutexId, MutexAcquire acq1, MutexAcquire acq2,
     int highPri, int lowPri
where
  highPri = highTask.getPriorityValue() and
  lowPri = lowTask.getPriorityValue() and
  highPri > lowPri and
  (highPri - lowPri) >= 3 and
  taskAcquiresMutex(highTask.getResolvedEntryFunction(), mutexId, acq1) and
  taskAcquiresMutex(lowTask.getResolvedEntryFunction(), mutexId, acq2) and
  acq1 != acq2 and
  // Exclude library code
  not acq1.getFile().getRelativePath().matches("%Middlewares%") and
  not acq2.getFile().getRelativePath().matches("%Middlewares%")
select acq1,
  "Priority inversion risk: mutex '" + mutexId +
  "' is shared between high-priority task (priority " + highPri.toString() +
  ") and low-priority task (priority " + lowPri.toString() +
  "). Gap of " + (highPri - lowPri).toString() +
  " — consider using a priority-inheritance mutex."
