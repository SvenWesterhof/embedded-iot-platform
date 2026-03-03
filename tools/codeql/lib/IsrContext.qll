/**
 * ISR context detection library.
 *
 * Identifies functions that execute in interrupt context on ESP32 and STM32,
 * enabling ISR safety checks.
 */

import cpp

/** A function that executes in ISR context (interrupt handler). */
class IsrFunction extends Function {
  IsrFunction() {
    // STM32 NVIC interrupt handlers (CubeMX naming convention)
    this.getName().matches("%_IRQHandler") or
    this.getName().matches("%_Handler") and
    this.getName() != "Reset_Handler" or
    // ESP32 IRAM_ATTR functions (run from IRAM, typically ISR context)
    this.getAnAttribute().getName() = "IRAM_ATTR" or
    // Common ISR callback naming patterns
    this.getName().matches("%_isr_%") or
    this.getName().matches("%_ISR_%") or
    this.getName().matches("isr_%")
  }
}

/**
 * Holds if `f` is reachable from an ISR function via the call graph.
 * This is transitive: if ISR calls A, and A calls B, then B is also
 * in ISR context.
 */
predicate isReachableFromIsr(Function f) {
  f instanceof IsrFunction
  or
  exists(Function caller |
    isReachableFromIsr(caller) and
    caller.calls(f)
  )
}

/**
 * A function call that occurs within ISR context (either directly in
 * an ISR function or transitively reachable from one).
 */
class IsrContextCall extends FunctionCall {
  IsrContextCall() {
    isReachableFromIsr(this.getEnclosingFunction())
  }
}
