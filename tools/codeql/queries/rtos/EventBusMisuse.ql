/**
 * @name Layer violation — lower layer directly calls higher layer
 * @description A function in a lower architectural layer directly calls
 *              a function in a higher layer, violating the event bus
 *              communication pattern. Lower layers should publish events,
 *              not call higher-layer functions directly.
 * @kind problem
 * @problem.severity warning
 * @precision medium
 * @id rtos/event-bus-misuse
 * @tags correctness
 *       architecture
 *       rtos
 */

import cpp
import lib.OsWrapper

from FunctionCall call, Function callee, File callerFile, File calleeFile,
     int callerRank, int calleeRank
where
  callee = call.getTarget() and
  callerFile = call.getFile() and
  calleeFile = callee.getFile() and
  callerRank = getFileLayerRank(callerFile) and
  calleeRank = getFileLayerRank(calleeFile) and
  // Lower layer calling higher layer
  callerRank > 0 and
  calleeRank > 0 and
  callerRank < calleeRank and
  // Exclude event bus itself (it's the legitimate upward communication channel)
  not callerFile.getBaseName().matches("event_bus%") and
  // Exclude callbacks and function pointers (these are the correct pattern)
  not call.getTarget().getName().matches("%callback%") and
  // Exclude standard library
  not calleeFile.getRelativePath().matches("%Middlewares%") and
  not calleeFile.getRelativePath().matches("%/build/%")
select call,
  "Layer violation: '" + call.getEnclosingFunction().getName() +
  "' (layer " + callerRank.toString() + ": " + callerFile.getBaseName() +
  ") directly calls '" + callee.getName() +
  "' (layer " + calleeRank.toString() + ": " + calleeFile.getBaseName() +
  "). Use event bus for upward communication."
