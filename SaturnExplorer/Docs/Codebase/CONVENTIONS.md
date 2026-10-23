# Implementation patterns and invariants

Inspected revision: `ff6a3b1fb1c54e4c0344613094ea65292b0023c2`.
These are patterns supported by current code/instructions, not proposed global style rules.

## Structure and naming

- Portable frontend types are generally under `sfe`; decompiler and AI code use nested namespaces.
  Core implementation uses `se`; exported host/source interfaces use the `se_` C vocabulary.
  Follow the surrounding module, rather than renaming unrelated code to enforce uniformity.
- A number of UI-free policy/model helpers sit beside ImGui panels: `TopBar`, `StepHaltMachine`,
  `ScrubState`, `BreakpointManager`, search runners and AI validators. Keep policy testable through
  the existing model when extending that behavior; App remains the cross-system coordinator.
- Core and savestate code build as C++14; inspect [CMakeLists.txt](../../CMakeLists.txt) and the
  checked-in MSBuild projects before introducing compiler-specific or later-standard dependencies.
- [CLAUDE.md](../../CLAUDE.md) asks comments to explain why a hardware quirk or guard exists.
  Existing architecture prose sometimes names conceptual responsibilities rather than files;
  use this knowledge base's verified locations and inspect source before editing.

## Errors, resources and persistence

The host ABI in [HostAbi.cpp](../../Core/src/HostAbi.cpp) validates arguments and catches
exceptions through boundary guards. Exported functions return `se_result`, counts or documented
sentinels; no C++ exception may escape the C ABI. Source callbacks are also guarded in capture
and destruction paths. Buffer-return APIs use caller allocation with size-query/fill semantics.

`Context` owns the source close callback after successful creation. App's retained data-source
table is borrowed and must be forgotten when closing. Images returned through the host API use
caller memory; GPU textures belong to platform/frontend lifetimes. Shared capture bytes are
owned copies or immutable shared buffers, not borrowed live memory.

[FileWrite.h](../../FrontEnd/src/FileWrite.h) provides checked writes, including final close.
[Settings.cpp](../../FrontEnd/src/Settings.cpp), `Settings::SaveTo`, stages and replaces settings
instead of truncating the previous file on failure. Reuse `Settings::ConfigSubDir`/`ConfigSubPath`
for persistent paths. Save dialogs use `SaveOutcome::{Saved,Cancelled,Failed}`; App's `ReportSave`
turns that into consistent status/log messages. Do not count cancellation as failure or claim
success before bytes reach disk. Multi-output disc/export publication has its own rollback
contract; do not assume every file-writing path is transactional.

## UI state and command routing

`TopBarViewModel` supplies state, `TopBarCommandEnabled` determines availability, and
`App::ExecuteTopBarCommand` performs effects. Native menus and shortcuts feed the same queue.
App rechecks enablement for each command in a batch. Data exports run after the historical
context swap; reuse pending requests instead of exporting from a toolbar click handler.

[PanelWidgets.h](../../FrontEnd/src/PanelWidgets.h) is the shared source for table row selection,
splitters and combo sizing. `RowSelectable` is required by CLAUDE.md for whole-row selection
because overlap and frame-height handling affect interactive cells. Shared widget tests do not
protect a new panel that bypasses the helper. `ImGuiHarness.h` supports headless interaction;
settle hover before testing drag/click behavior where the widget requires it.

## Threading and freshness

- ImGui/App/context derivation run on the UI path. Driver capture pinning protects a coherent
  source read, not arbitrary concurrent mutation/query of a core context.
- Workers own their input. The decompiler receives `MemorySnapshot`; RAM/file search runners
  receive copied requests. Cancellation, queue replacement, completion routing and joining belong
  to the relevant runner, not an ad hoc UI flag. Read its current Stop/shutdown behavior first.
- UI results must match current identity. A source pointer can be reused; a frame number can
  repeat or move backwards; an emulator can reconnect. Use the subsystem's source/session,
  capture/derive, edit, BIOS and metadata identities rather than selecting a convenient counter.
- Keep captured generation distinct from connection generation. On reconnect, a new connection
  does not make the old displayed snapshot belong to the new emulator. Stop/call-stack data is
  tied to the displayed capture; writes and restores need acknowledgement/capability handling.
- When scrubbing, data panels read the historical context while controls operate on live state.
  Ring indexes shift; stable recorded-frame identity is required for comparisons and staged edits.

Evidence: [App.cpp](../../FrontEnd/src/App.cpp), `BuildUI`, `CloseData`, `AdoptNewEmulatorInstance`;
[StateSources](Systems/StateSources.md), [Decompiler](Systems/Decompiler.md),
[MemoryAndHistory](Systems/MemoryAndHistory.md).

## Hardware and address rules

Reuse [Sh2Address.h](../../FrontEnd/src/Sh2Address.h) for SH-2 mirrors/canonical partitions,
[SaturnRegions.h](../../FrontEnd/src/SaturnRegions.h) and memory-region traits for region bounds,
[ByteOrder.h](../../Core/src/ByteOrder.h) for big-endian hardware bytes. Device memory reads can
have side effects. Ghidra's load preservation and hardware-volatility settings are intentional
semantics, not generic optimization cleanup. Exact executing addresses and canonical UI
navigation addresses have different uses; preserve that distinction in prototypes and PC hints.

## Testing and maintenance

Tests are mostly standalone executable fixtures with CTest registration in
[CMakeLists.txt](../../CMakeLists.txt). Consult the target definition: some are platform-,
compiler- or decompiler-gated. Reuse `FakeVdpSource`, `FakeLiveServer`, `ImGuiHarness` and
`DecompilerTestSupport` where their contracts match the behavior under test. A synthetic capture
or `.sedump` is the reproducible rendering oracle; a socket fixture tests protocol semantics
without proving an actual emulator hook is correct. Fresh hardware/platform verification is
still needed for those integration changes.

From `SaturnExplorer/`: configure/build with [BUILD.md](../../BUILD.md), then
`ctest --test-dir build --output-on-failure`; select the relevant family from subsystem pages.
Documentation-only changes require reference/link/scope validation, not a full native application
build. Keep the feature map, subsystem descriptions and changed APIs/flows consistent in the same
code change. The source remains authoritative. Read [README](README.md) for the update policy.
