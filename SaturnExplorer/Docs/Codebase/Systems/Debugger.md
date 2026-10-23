# Debugger execution, assembly, watches, and call stacks

Inspected revision: `ff6a3b1fb1c54e4c0344613094ea65292b0023c2` (2026-10-20).
Evidence status: behavior below is verified from source unless explicitly qualified. This is a navigation map; source remains authoritative.

## Purpose and responsibilities

The frontend coordinates instruction-level debugging of the two SH-2 CPUs, while its models remain independent of ImGui and the emulator transport. It provides execution and memory breakpoints, tracepoint logging, assembly navigation, register inspection, typed memory watches, call stacks and function naming. The core supplies captured registers and memory; live commands travel through the driver. See [state sources](StateSources.md) and [memory/history](MemoryAndHistory.md).

## Source locations and important types

| Responsibility | Source and symbols |
|---|---|
| Orchestration and live stop policy | [App.cpp](../../../FrontEnd/src/App.cpp): `App::BuildUI`, `Continue`, `StepInto`, `StepOver`, `StepOut`, `RunToTransient`, `SyncBreakpointsToLive`, `SyncTracepointsToLive`, `DrainTraceEvents` |
| Step and halt state | [Debug/StepHaltMachine.h](../../../FrontEnd/src/Debug/StepHaltMachine.h), [implementation](../../../FrontEnd/src/Debug/StepHaltMachine.cpp): `StopReport`, `StepOutcome`, `StepHaltMachine` |
| User breakpoints | [Debug/BreakpointManager.h](../../../FrontEnd/src/Debug/BreakpointManager.h), [implementation](../../../FrontEnd/src/Debug/BreakpointManager.cpp): `Breakpoint`, `BpKind`, `BreakpointManager` |
| Non-halting actions | [Debug/ExecutionActions.h](../../../FrontEnd/src/Debug/ExecutionActions.h), [implementation](../../../FrontEnd/src/Debug/ExecutionActions.cpp): `ExecutionAction`, `ActionEffects`, `ExecutionActions`, `EmulatorAppliesRepeat` |
| Assembly view | [AssemblyPanel.cpp](../../../FrontEnd/src/AssemblyPanel.cpp): `AssemblyPanel::Draw`, `GoTo`, `LoadPages`; [AsmCodeWindow.h](../../../FrontEnd/src/AsmCodeWindow.h); [Debug/Sh2Disasm.cpp](../../../FrontEnd/src/Debug/Sh2Disasm.cpp): `Sh2DecodeAt`; [Sh2Operands.cpp](../../../FrontEnd/src/Sh2Operands.cpp) |
| Register display | [App.cpp](../../../FrontEnd/src/App.cpp): `DrawRegisters`, `DrawSh2Registers`; [Debug/Sh2RegInfo.h](../../../FrontEnd/src/Debug/Sh2RegInfo.h): `Sh2RegTable`, `Sh2RegValue` |
| Watch model and UI | [Debug/WatchList.cpp](../../../FrontEnd/src/Debug/WatchList.cpp): `WatchList`, `SimpleExpressionResolver::Resolve`, `FormatWatchValue`; [WatchPanel.cpp](../../../FrontEnd/src/WatchPanel.cpp): `WatchPanel::Draw`, `Refresh` |
| Conditions and log formatting | [Debug/ConditionEval.cpp](../../../FrontEnd/src/Debug/ConditionEval.cpp): `ConditionEval`, `ConditionValidate`; [Debug/FormatString.h](../../../FrontEnd/src/Debug/FormatString.h): `IFormatContext`; [LogPanel.cpp](../../../FrontEnd/src/LogPanel.cpp) |
| Call-chain model and naming | [Debug/CallStack.cpp](../../../FrontEnd/src/Debug/CallStack.cpp): `CallStack::Reconstruct`, `SetConfirmed`, `ReconcileHeuristicTail`, `ChooseStepOutTarget`, `FunctionNames`; [App.cpp](../../../FrontEnd/src/App.cpp): `BuildCallStack`, `RebuildCallStack`, `DrawCallStack`, `GoToFrame` |

## Entry points and execution flow

### Stop detection and stepping

1. `App::BuildUI` captures state as needed, synchronizes breakpoint/action generations, drains events, then asks `se_live_get_stop` and `se_live_get_stop_seq` for a stop report.
2. `StepHaltMachine::Observe` interprets halt identity and in-flight steps. It reports whether this is a step arrival, an unwanted arrival by the other CPU, or an echo of an already declined halt.
3. App applies policy: condition guards, logging watchpoints and mixed execution/watchpoint causes. A false execution condition cannot discard an independently plausible halting watchpoint. `WatchCausesAtHalt` reuses `Sh2MayAccessRange` and captured registers; unreadable instructions conservatively leave watchpoints as possible causes.
4. A surfaced stop sets `mbPaused`, identifies the halted CPU, marks the call stack dirty and reveals the paused workspace. Ordinary breakpoint stops also reveal Assembly, Registers and Breakpoints. Completed steps avoid repeatedly stealing Assembly focus.
5. `StepInto` sends `se_live_step_insn`, while `StepOver` decodes the current instruction: `bsr`/`bsrf`/`jsr` run to `PC + 4`, past the delay slot; other instructions use Step Into. `StepOut` builds a fresh call stack and uses `ChooseStepOutTarget`.
6. `RunToTransient` installs a temporary execution breakpoint **before** resuming. Its target CPU is recorded by `StepHaltMachine`; a different CPU reaching the shared PC breakpoint is resumed without completing the step. Arrival retires the temporary breakpoint and triggers synchronization.

`StepHaltMachine` does not resume the emulator, evaluate expressions, log accesses or open panels. Those side effects remain in App. Reuse this separation when adding run-control behavior.

### Tracepoint to Log

`AssemblyPanel::Request::editTracepoint` / App's action UI → `ExecutionActions` mutation → generation-triggered `SyncTracepointsToLive` → driver events → `DrainTraceEvents` → condition/repeat acceptance → `FormatEvaluate` → `mLog.Tracepoint`.

`DrainTraceEvents` formats registers captured **at the hit**, but memory dereferences use the current frontend snapshot. This timing distinction is real and must survive any new display or export. For unconditional tracepoints on server v21+, repeat filtering happens in the emulator; guarded or older-server actions are counted by `AcceptHit`. Do not filter Every-N a second time. Enabled tracepoints are serialized first when the descriptor limit is exceeded; App warns about dropped descriptors.

### Watch and assembly navigation

`WatchPanel::Draw` schedules a roughly 10 Hz `Refresh`. It resolves changed address text, batches enabled memory reads through `IMemoryBackend`, then calls `FormatWatchValue` for type interpretation and change highlighting. `SimpleExpressionResolver` accepts a base address plus at most one numeric `+`/`-` offset and rejects overflow/underflow. It is **not** the condition-expression parser.

`AssemblyPanel::Draw` reads registers through `se_get_sh2_regs` and code through the backend. It caches decoded pages, distinguishes source changes through `SourceId`, and returns user intentions in `AssemblyPanel::Request`; App performs run-control, hex navigation, disc search and decompiler actions. `GoTo` is browsing navigation; `Reveal` resumes Follow PC without adding browsing history.

### Call stacks

`App::BuildCallStack` uses live shadow frames from `se_live_poll_callstack` when available, then adds `ReconcileHeuristicTail` for callers predating recording. Static sources and scrubbed frames use `Reconstruct`; the current emulator's shadow stack must not be combined with old recorded memory.

`CallStackFrame` distinguishes `Confirmed`, `Probable` and `Heuristic` confidence; `functionKnown` distinguishes an actual entry point from an address merely inside a caller. `returnRecorded` is separate from confidence: a known current PC does not prove PR is the function's return address. `GoToFrame` navigates to meaningful code even if the entry is unknown.

## Interactions, state, and lifetime

App owns the managers, panels, call stack, function names and shared `ContextBackend`. Managers expose mutation generations for resynchronization; hit counts are runtime state, rather than reasons to resend an unchanged set. `ResetSessionDebugState` forces fresh sync and resets counts for a new emulator. `CloseData` clears stale call stacks. `StepHaltMachine::ResetHalt` deliberately preserves a transient installed in the peer; `ResetForNewEmulator` drops it and forces sync because this is a different peer.

Watches, assembly comments and function names have explicit load/save methods called by App initialization/shutdown; do not assume breakpoints and tracepoint stores have equivalent persistence. `FunctionNames::Generation` is also decompiler input invalidation, because names are supplied to the engine, not merely overlaid after printing. See the [decompiler subsystem](Decompiler.md).

## Extension points

- Add run-control policy in App and state transitions in `StepHaltMachine`; preserve the observable stop/echo distinction.
- Extend breakpoint storage/sync through `BreakpointManager` and App's sync path. Tracepoint action types are a separate model; the internal `Normalize` helper in `ExecutionActions.cpp` clears effects that have no implementation.
- Add address-expression syntax behind `IExpressionResolver`; add boolean guard syntax in `ConditionEval`. Their contracts and cache behavior differ.
- Reuse `IMemoryBackend` and `FormatWatchValue` for captured memory values; reuse `Sh2RegTable` for register metadata, and `FunctionNames` for shared function naming.
- For paused pseudocode variable values, first inspect the decompiler mapping/value model, then these register/backend and frame-origin APIs. Existing address watches do not establish a decompiled local's storage or prove an older caller frame's registers.

## Constraints and confirmed limitations

- Execution breakpoints are shared by address across master and slave; do not reintroduce CPU-specific duplicate entries (`BreakpointManager`). Its lookups compare stored addresses literally; the later App stop-banner logic also checks canonical mirrors, so do not assume all breakpoint policies already normalize aliases. Instruction stepping targets the CPU that halted, regardless of the panel's current CPU.
- At most one step may be in flight. Resume echoes need halt sequence numbers when available; old peers fall back to PC comparison and cannot distinguish every same-PC halt (`StepHaltMachine`).
- Step Out requires a recorded return and consistency checks; PR can be overwritten by nested calls. A heuristic caller chain is useful for navigation, not proof of a safe return target (`ChooseStepOutTarget`).
- SH-2 registers are read-only in `DrawSh2Registers`; the public host API provides no register write operation.
- Condition evaluation fails open on syntax/resolution errors, so a broken guard does not silently swallow a stop (`ConditionEval`).
- Tracepoint memory is sampled later than its captured hit registers. Non-log action effects are cleared by `Normalize`, rather than implied by editor/model fields.

## Testing and verification

From `SaturnExplorer/`, configure/build as described in [BUILD.md](../../../BUILD.md), then select relevant registered tests:

```sh
ctest --test-dir build --output-on-failure -R 'StepHaltMachine|BreakpointManager|ExecutionActions|ConditionEval|ExpressionResolver|CallStack|FunctionNames|AssemblyPanel|Sh2Decode|Sh2OperandHover|Sh2RegInfo|PanelInteraction'
```

The corresponding `FrontEnd/tests/*Tests.cpp` files isolate models or use headless ImGui. Important examples: [StepHaltMachineTests.cpp](../../../FrontEnd/tests/StepHaltMachineTests.cpp) exercises halt identity/races; [BreakpointManagerTests.cpp](../../../FrontEnd/tests/BreakpointManagerTests.cpp) exercises guards and shared breakpoint identity; [CallStackHeuristicTests.cpp](../../../FrontEnd/tests/CallStackHeuristicTests.cpp) exercises reconstruction and Step Out; [PanelInteractionTests.cpp](../../../FrontEnd/tests/PanelInteractionTests.cpp) exercises input routing. Live exporter stop races also have [LiveHaltRaceTests.cpp](../../../FrontEnd/tests/LiveHaltRaceTests.cpp). Availability of targets depends on build options in [CMakeLists.txt](../../../CMakeLists.txt).

Investigation coverage: frontend orchestration and models were traced; real Windows/macOS gameplay, every operand decoder and all emulator hook internals were not independently exercised in this documentation pass.

## Related documentation

[Execution actions](../../../EXECUTION_ACTIONS.md), [call stack design](../../../CALL_STACK.md), [memory and history](MemoryAndHistory.md), [state sources](StateSources.md), [feature map](../FEATURE_MAP.md).
