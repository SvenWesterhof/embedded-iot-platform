/**
 * @name Memory leak — allocation not freed on all paths
 * @description A heap allocation (malloc/calloc/pvPortMalloc) is not freed
 *              on all paths to the end of the enclosing function.
 * @kind problem
 * @problem.severity warning
 * @precision medium
 * @id rtos/memory-leak
 * @tags correctness
 *       rtos
 *       memory
 *
 * Parameterized: The correlator may scope this to a specific allocation
 * site using codeql_params.alloc_function and codeql_params.variable_name.
 * When run standalone, it checks all allocations.
 */

import cpp
import lib.FreeRTOS
import semmle.code.cpp.dataflow.DataFlow

/**
 * Holds if `alloc` is a heap allocation whose result is stored in a local
 * variable, and there exists a path from `alloc` to the function exit where
 * the variable is not freed.
 */
predicate leakedAllocation(HeapAllocation alloc, LocalVariable v) {
  // The allocation result flows into a local variable
  exists(AssignExpr assign |
    DataFlow::localFlow(DataFlow::exprNode(alloc), DataFlow::exprNode(assign.getRValue())) and
    assign.getLValue().(VariableAccess).getTarget() = v
  ) and
  // The variable is in the same function as the allocation
  v.getFunction() = alloc.getEnclosingFunction() and
  // No free/vPortFree call on this variable exists in the same function
  not exists(HeapDeallocation dealloc |
    dealloc.getEnclosingFunction() = alloc.getEnclosingFunction() and
    exists(VariableAccess va |
      va = dealloc.getAnArgument() and
      va.getTarget() = v
    )
  )
}

from HeapAllocation alloc, LocalVariable v
where
  leakedAllocation(alloc, v) and
  // Exclude library/generated code
  not alloc.getFile().getRelativePath().matches("%Middlewares%") and
  not alloc.getFile().getRelativePath().matches("%/build/%") and
  not alloc.getFile().getRelativePath().matches("%/Drivers/%")
select alloc,
  "Heap allocation via '" + alloc.getTarget().getName() +
  "' stored in '" + v.getName() +
  "' may not be freed on all paths to function exit."
