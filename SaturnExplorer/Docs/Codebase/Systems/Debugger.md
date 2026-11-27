# Debugger execution, assembly, watches, and call stacks

Inspected revision: `ff6a3b1fb1c54e4c0344613094ea65292b0023c2` (2026-10-20).
Pseudocode navigation/Watch integration rechecked against `codex/decompiler-variable-inspection` (2026-10-20).
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
| In-place assembler | [Debug/Sh2Asm.cpp](../../../FrontEnd/src/Debug/Sh2Asm.cpp): `Sh2Assemble`, `Sh2AsmContext`, `Sh2AsmResult`; editing UI in `AssemblyPanel` (`BeginInstructionEdit`, `BeginLiteralEdit`, `BuildAsmContext`, `SetSymbolResolver`); name lookup [Debug/FunctionSymbol.h](../../../FrontEnd/src/Debug/FunctionSymbol.h): `ResolveFunctionSymbol` |
| Struct views | [StructPanel.cpp](../../../FrontEnd/src/StructPanel.cpp): `StructPanel::Draw`, `LoadSession`, `SaveSession`, `RequestViewAt`; model and text format [Debug/StructDefs.cpp](../../../FrontEnd/src/Debug/StructDefs.cpp): `StructLibrary`, `StructDef`, `StructView`, `DecodeField`, `EncodeField`; App: `App::DrawStructs` |
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

### Struct views

The Structs window shows `view label = Type[count] @ expression;` entries from `saturn_structs.txt` (struct definitions and views in one C-like text file in the work folder). Each view's expression resolves through the same `IExpressionResolver` as Watch. Fields, nested structs and array elements are tree rows; a view with `count > 1` can also be a table with one element per row (scalar columns capped at 64). A pointer-to-struct field expands lazily to its target, to a depth of 8, and a repeated address is shown as "(shown above)". Only expanded, visible rows are read, through `IMemoryBackend::ReadMemoryBatch`. Edit Value encodes big-endian bytes and commits them as one `WriteMemory` call; a refused or short write keeps the old value and shows the reason, and a `SourceId` change drops the edit.

`App::DrawStructs` passes the panel Watch's arguments (backend, resolver, platform, breakpoints, frame time) plus two out-params. A hex-jump address is revealed in the Hex Editor as Watch's is. An "Add to Watch" request on a scalar leaf goes to `WatchPanel::AddWatch`. Watch's row menu "View as Struct..." fills `WatchPanel::ViewAsStructRequest` (address and name); App forwards it to `StructPanel::RequestViewAt` and shows the window. With no struct defined yet the Definitions editor opens and the view is created when text that defines one is applied.

Layout: a saved `imgui.ini` that predates the window opens it floating, so `App::AdoptNewPanels` docks Structs into Watch's node (`AdoptPanelsBeside` in [LayoutAdoption.h](../../../FrontEnd/src/LayoutAdoption.h), shared with Memory Compare; `LayoutAdoptionTests`). Profiler, Tracepoints and the other later debugger windows are not in that table. The toolbar's status line (load error, "Editing ...", notice) is one reserved line.

Persistence: `LoadSession` runs at startup and `SaveSession` at shutdown, like Watch. Unlike Watch, App also saves while running: `DrawStructs` calls `SaveSession` at most every 5 seconds while `NeedsSave()` is true. A refused save (a lock, a full disk, or a file that failed to load) is logged once per distinct message, not every retry. A file that failed to load is held as text and never overwritten until definitions that parse are applied. A view whose struct was deleted is kept as a `//!broken view` comment line so it survives a restart.

### Editing an instruction in place

The Assembly row menu (Assemble..., F2, double-click on the Instr or Bytes cell) opens a one-line editor seeded with the row's own text. `Sh2Assemble` turns one line into one 16-bit opcode, using the same spelling `Sh2Decode` prints plus a few aliases, and decodes the result back and compares it before returning bytes. The panel then commits the opcode as a single two-byte `IMemoryBackend::WriteMemory`, so it is one poke or one staged record, and it drops its page cache because a backend write does not move `SourceId`. Rows holding an unambiguous PC-relative load also offer Edit Literal Value..., which writes the 2 or 4 bytes at the effective address through the same path.

`App::Initialize` gives the panel a symbol resolver (`ResolveFunctionSymbol` over `FunctionNames::All()` plus the default `sub_XXXXXX` spelling; a name held by several addresses is refused with the addresses listed). `App::DrawAssembly` copies `Request::asmNote` into the Log when `Request::assembled` is set.

Constraints:
- The notice line under the Assembly header is a fixed, always-reserved single line (`FixedStatusLine` in `PanelWidgets.h`: ellipsis, full text in a tooltip) so a notice appearing or expiring never moves the rows; the Structs panel's load-error, editing and notice texts share one such line.
- One instruction per edit. The assembler never allocates or moves a literal pool: `mov.l #imm32,rN` is refused with a message pointing at Edit Literal Value or the `@(addr)` form.
- Delay-slot problems are warnings, not errors, because the same row can also be reached directly.
- The "also loaded by N other instructions" count for a literal is a heuristic decode of the instructions that could reach the slot.
- Editing a running CPU does not pause it. The poke lands at the emulator's next frame gate. Whether the emulated instruction cache can keep running the old opcode after a poke was not investigated.
- **Stale prefetch while halted.** A halted SH-2 has already fetched the instructions ahead of its PC, and a poke does not reach them: patching the instruction at PC+2 ran the OLD opcode once on a real Mednafen, while patches further away ran correctly. The emulator is not changed; `AssemblyPanel::CommitEdit` appends a warning to the notice and to `Request::asmNote` (so to the Log) when a successful instruction write overlaps the fetched window of the CPU it is halted on. The window comes from how the panel knows the PC: the halted PC is the decode stage (`PC_ID`; `GSREG_RPC`, the next fetch address, is 4 bytes ahead of it), the stage behind it holds PC+2, so `[PC, PC+4)` is always fetched. With the instruction cache emulated, `FetchIF` reads a longword at a time and keeps the other half in `IBuffer`, so when PC+2 is longword aligned (`PC & 2`) the half at PC+4 is already buffered too: `[PC, PC+6)`. Only the PC+2 case was seen on a real emulator; the rest is read from `sh7095.inc`. Literal edits are not affected (data is read when the load executes), nor is a CPU that is running or the one not halted.
- An edit is cancelled when `SourceId` or the CPU selection changes.

The pseudocode panel adds fixed-address shortcuts through `App::UpdateDecompiler` into the same
Watch window. Storage of a pointer object and memory at its captured target are distinct actions.
Only complete supported captured WRAM locations pass the inspector's stricter policy, and their
aliases are canonicalized before requesting a watch. Subsequent Watch samples use the ordinary
current-view backend and change highlighting; the shortcut neither tracks a source occurrence
nor follows a changing pointer. **Open Registers** selects the pseudocode CPU, the SH-2 tab and
focus in the existing Registers window without altering its renderer or current-view behavior.

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
- Reuse `IMemoryBackend` and `FormatWatchValue` for ordinary Watch memory values; pseudocode previews instead use `CapturedValueInspection` over the matched immutable snapshot. Reuse `Sh2RegTable` for register metadata and `FunctionNames` for shared function naming.
- For paused pseudocode variable values, first inspect the decompiler mapping/value model, then these register/backend and frame-origin APIs. Existing address watches do not establish a decompiled local's storage or prove an older caller frame's registers.

## Constraints and confirmed limitations

- Execution breakpoints are shared by address across master and slave; do not reintroduce CPU-specific duplicate entries (`BreakpointManager`). Its lookups compare stored addresses literally; the later App stop-banner logic also checks canonical mirrors, so do not assume all breakpoint policies already normalize aliases. Instruction stepping targets the CPU that halted, regardless of the panel's current CPU.
- At most one step may be in flight. Resume echoes need halt sequence numbers when available; old peers fall back to PC comparison and cannot distinguish every same-PC halt (`StepHaltMachine`).
- Step Out requires a recorded return and consistency checks; PR can be overwritten by nested calls. A heuristic caller chain is useful for navigation, not proof of a safe return target (`ChooseStepOutTarget`).
- SH-2 registers are read-only in `DrawSh2Registers`; the public host API provides no register write operation.
- Condition evaluation fails open on syntax/resolution errors, so a broken guard does not silently swallow a stop (`ConditionEval`).
- Tracepoint memory is sampled later than its captured hit registers. Non-log action effects are cleared by `Normalize`, rather than implied by editor/model fields.

## Testing and verification

From `SaturnExplorer/`, configure/build as described in [BUILD.md](../../BUILD.md), then select relevant registered tests:

```sh
ctest --test-dir build --output-on-failure -R 'StepHaltMachine|BreakpointManager|ExecutionActions|ConditionEval|ExpressionResolver|CallStack|FunctionNames|AssemblyPanel|Sh2Asm|FunctionSymbol|StructDefs|StructPanel|LayoutAdoption|WatchPanel|Sh2Decode|Sh2OperandHover|Sh2RegInfo|PanelInteraction'
```

The corresponding `FrontEnd/tests/*Tests.cpp` files isolate models or use headless ImGui. Important examples: [StepHaltMachineTests.cpp](../../../FrontEnd/tests/StepHaltMachineTests.cpp) exercises halt identity/races; [BreakpointManagerTests.cpp](../../../FrontEnd/tests/BreakpointManagerTests.cpp) exercises guards and shared breakpoint identity; [CallStackHeuristicTests.cpp](../../../FrontEnd/tests/CallStackHeuristicTests.cpp) exercises reconstruction and Step Out; [PanelInteractionTests.cpp](../../../FrontEnd/tests/PanelInteractionTests.cpp) exercises input routing. Live exporter stop races also have [LiveHaltRaceTests.cpp](../../../FrontEnd/tests/LiveHaltRaceTests.cpp). Availability of targets depends on build options in [CMakeLists.txt](../../../CMakeLists.txt).

The assembler's exhaustive round trip (every opcode at five addresses against `Sh2Decode`) is `Sh2AsmTests`; the panel's editing is covered by `AssemblyPanelTests`; an assembled write against a real core context is `TestAssembledWrite` in `MemoryBackendTests`.

Investigation coverage: frontend orchestration and models were traced; real Windows/macOS gameplay, every operand decoder and all emulator hook internals were not independently exercised in this documentation pass.

## Related documentation

[Execution actions](../../EXECUTION_ACTIONS.md), [call stack design](../../CALL_STACK.md), [memory and history](MemoryAndHistory.md), [state sources](StateSources.md), [feature map](../FEATURE_MAP.md).
