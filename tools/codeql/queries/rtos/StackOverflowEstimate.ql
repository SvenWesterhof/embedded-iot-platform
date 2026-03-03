/**
 * @name Potential task stack overflow
 * @description A task's declared stack size may be insufficient for its
 *              call depth. Estimates stack usage by summing local variable
 *              sizes across all transitively called functions.
 * @kind problem
 * @problem.severity warning
 * @precision low
 * @id rtos/stack-overflow-estimate
 * @tags correctness
 *       rtos
 *       memory
 */

import cpp
import lib.FreeRTOS

/**
 * Estimate the local variable stack usage of a function (in bytes).
 * Sums the sizes of all non-static local variables.
 */
int estimateLocalStackUsage(Function f) {
  result = sum(LocalVariable v |
    v.getFunction() = f and
    not v.isStatic()
  | v.getType().getSize())
}

/**
 * Estimate worst-case stack usage for a task entry function by summing
 * local variable sizes of the entry function and all transitively called
 * functions. This is an over-approximation (assumes all functions on the
 * call graph are active simultaneously).
 */
int estimateTaskStackUsage(Function entry) {
  result = estimateLocalStackUsage(entry) +
    sum(Function callee |
      entry.calls+(callee) and callee != entry
    | estimateLocalStackUsage(callee))
}

from TaskCreation task, Function entry, int declaredStack, int estimatedUsage
where
  entry = task.getEntryFunction().(FunctionAccess).getTarget() and
  declaredStack = task.getStackSize().getValue().toInt() and
  declaredStack > 0 and
  estimatedUsage = estimateTaskStackUsage(entry) and
  // Flag if estimated usage exceeds 60% of declared stack
  // (FreeRTOS needs headroom for context save, interrupts, etc.)
  estimatedUsage * 100 / declaredStack > 60 and
  // Only report tasks in application code
  not task.getFile().getRelativePath().matches("%Middlewares%") and
  not task.getFile().getRelativePath().matches("%freertos%")
select task,
  "Task '" + entry.getName() + "' has " + declaredStack.toString() +
  "B stack but estimated local variable usage is " +
  estimatedUsage.toString() + "B (" +
  (estimatedUsage * 100 / declaredStack).toString() +
  "% of stack). Consider increasing stack size — FreeRTOS needs " +
  "headroom for context save frames and interrupt nesting."
