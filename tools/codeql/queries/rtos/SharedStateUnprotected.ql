/**
 * @name Unprotected access to shared state
 * @description A global/static variable or struct field is accessed (read or
 *              written) from multiple functions without mutex protection,
 *              creating a potential data race between RTOS tasks.
 * @kind problem
 * @problem.severity error
 * @precision medium
 * @id rtos/shared-state-unprotected
 * @tags correctness
 *       concurrency
 *       rtos
 *       race-condition
 */

import cpp
import lib.FreeRTOS

/**
 * Holds if function `f` is a CubeMX-generated init or an IRQ handler that
 * runs before the RTOS scheduler, making mutex protection unnecessary.
 */
predicate isPreSchedulerOrVendorFunction(Function f) {
  f.getName().matches("MX_%_Init") or
  f.getName() = "SystemClock_Config" or
  f.getName() = "main" or
  f.getName().matches("HAL_%MspInit%") or
  f.getName().matches("HAL_%MspDeInit%") or
  f.getName().matches("%_IRQHandler") or
  f.getName().matches("bl_%") or
  f.getName().matches("SEGGER_%") or
  f.getName().matches("_DoInit%") or
  f.getName().matches("_PostTerminal%")
}

/** Holds if the file is vendor/generated/library code. */
predicate isExcludedFile(File file) {
  file.getRelativePath().matches("%Middlewares%") or
  file.getRelativePath().matches("%/build/%") or
  file.getRelativePath().matches("%freertos%") or
  file.getRelativePath().matches("%FreeRTOS%") or
  file.getRelativePath().matches("%/Drivers/%") or
  file.getRelativePath().matches("%SEGGER%") or
  file.getRelativePath().matches("%Bootloader%") or
  file.getRelativePath().matches("%Core/Src/stm32%") or
  file.getRelativePath().matches("%Core/Src/system_%")
}

/** Holds if the variable name is a known HAL peripheral handle. */
predicate isHalPeripheralHandle(string name) {
  name.matches("huart%") or
  name.matches("hspi%") or
  name.matches("hi2c%") or
  name.matches("hrtc%") or
  name.matches("htim%") or
  name.matches("hdma_%") or
  name = "SystemCoreClock" or
  name.matches("uwTick%")
}

/**
 * Holds if the expression `e` is NOT protected by a mutex
 * (no os_mutex_take/xSemaphoreTake call precedes it in the same function
 * without a corresponding os_mutex_give/xSemaphoreGive before it).
 */
predicate isUnprotectedExpr(Expr e) {
  not exists(MutexAcquire ma |
    ma.getEnclosingFunction() = e.getEnclosingFunction() and
    ma.getLocation().getStartLine() < e.getLocation().getStartLine()
  )
}

// =========================================================================
// Part 1: Top-level global/static variables (original logic)
// =========================================================================

/** A global or file-static variable that could be shared between tasks. */
class SharedVariable extends Variable {
  SharedVariable() {
    (this instanceof GlobalVariable or this.isStatic()) and
    not this.isConst() and
    not this.getType().isConst() and
    not isExcludedFile(this.getFile()) and
    not isHalPeripheralHandle(this.getName())
  }
}

predicate hasUnprotectedCrossTaskAccess(SharedVariable v, VariableAccess unprotected) {
  exists(Function f1, Function f2 |
    f1 != f2 and
    exists(VariableAccess va1 | va1.getTarget() = v and va1.getEnclosingFunction() = f1) and
    exists(VariableAccess va2 | va2.getTarget() = v and va2.getEnclosingFunction() = f2)
  ) and
  exists(VariableAccess write |
    write.getTarget() = v and
    write.isUsedAsLValue()
  ) and
  unprotected.getTarget() = v and
  isUnprotectedExpr(unprotected)
}

// =========================================================================
// Part 2: Struct field access on global/static structs
// =========================================================================

/**
 * A FieldAccess on a global or file-static struct variable.
 * This captures patterns like `state.tx_in_progress` where `state` is
 * a file-static struct and `tx_in_progress` is a field.
 */
class SharedFieldAccess extends FieldAccess {
  Variable structVar;

  SharedFieldAccess() {
    // The qualifier is a variable access to a global or file-static struct
    structVar = this.getQualifier().(VariableAccess).getTarget() and
    (structVar instanceof GlobalVariable or structVar.isStatic()) and
    not structVar.isConst() and
    not isExcludedFile(structVar.getFile()) and
    not isHalPeripheralHandle(structVar.getName())
  }

  /** Get the struct variable being accessed. */
  Variable getStructVariable() { result = structVar }

  /** Get a descriptive name: "struct.field". */
  string getQualifiedName() {
    result = structVar.getName() + "." + this.getTarget().getName()
  }
}

/**
 * Holds if a struct field (identified by struct variable + field name) is
 * accessed from multiple functions with at least one unprotected write.
 */
predicate hasUnprotectedFieldAccess(
  Variable structVar, Field field, SharedFieldAccess unprotected
) {
  unprotected.getStructVariable() = structVar and
  unprotected.getTarget() = field and
  // Accessed from at least 2 different functions
  exists(SharedFieldAccess fa1, SharedFieldAccess fa2 |
    fa1.getStructVariable() = structVar and fa1.getTarget() = field and
    fa2.getStructVariable() = structVar and fa2.getTarget() = field and
    fa1.getEnclosingFunction() != fa2.getEnclosingFunction()
  ) and
  // At least one access is a write
  exists(SharedFieldAccess write |
    write.getStructVariable() = structVar and
    write.getTarget() = field and
    write.isUsedAsLValue()
  ) and
  // The flagged access is unprotected
  isUnprotectedExpr(unprotected)
}

// =========================================================================
// Combined results
// =========================================================================

from Expr unprotected, string message
where
  (
    // Part 1: top-level variable access
    exists(SharedVariable v, VariableAccess va |
      va = unprotected and
      hasUnprotectedCrossTaskAccess(v, va) and
      message = "Unprotected access to shared variable '" + v.getName() +
        "' in function '" + va.getEnclosingFunction().getName() +
        "'. This variable is also accessed from other functions — potential data race."
    )
    or
    // Part 2: struct field access
    exists(Variable sv, Field f, SharedFieldAccess sfa |
      sfa = unprotected and
      hasUnprotectedFieldAccess(sv, f, sfa) and
      message = "Unprotected access to shared field '" + sfa.getQualifiedName() +
        "' in function '" + sfa.getEnclosingFunction().getName() +
        "'. This field is also accessed from other functions — potential data race."
    )
  ) and
  // Common exclusions at the access site
  not isExcludedFile(unprotected.getFile()) and
  not isPreSchedulerOrVendorFunction(unprotected.getEnclosingFunction())
select unprotected, message
