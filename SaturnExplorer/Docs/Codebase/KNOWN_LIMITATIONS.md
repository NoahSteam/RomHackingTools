# Confirmed limits, discrepancies and investigation gaps

Inspected revision: `ff6a3b1fb1c54e4c0344613094ea65292b0023c2`.
A declared scope limit or intentional modeling choice is not automatically a defect.
Detailed source evidence and test navigation are in the linked subsystem pages.

## Verified implementation limits

| Area | Verified constraint | Engineering implication / evidence |
|---|---|---|
| Hardware reconstruction | Vertical mosaic is unmodeled; sprite previews omit occlusion/destination-dependent blending | Use the exact render path as oracle, not an isolated thumbnail; [Graphics](Systems/Graphics.md), `Vdp2Compositor.h`, `SeHost.h` |
| 3D inspection | Background planes sit behind sprite geometry; original game-world geometry is not recovered | A front view cannot express every per-pixel 2D priority relation; [Graphics](Systems/Graphics.md), `Context::CollectLayerPlanes` |
| 2D selection | Reverse-order geometric quad picking, not final visible-texel picking | A transparent/covered texel can still belong to a selectable quad; [Graphics](Systems/Graphics.md), `Context::HitTest` |
| Sound preview | Frozen slot/envelope/pan reconstruction; DSP omitted; reverse/alternating loops preview forward | Preview helps identify captured sounds, not reproduce the exact emulator mix; [Frontend](Systems/Frontend.md), `ScspMix.h` |
| Pseudocode input | Desktop engine analyzes captured work RAM with optional BIOS, not arbitrary uncaptured bus memory | Missing code is not silently live-fetched; [Decompiler](Systems/Decompiler.md), `MemorySnapshot`, `CaptureLoadImage` |
| Pseudocode values | Result tokens lack evaluable variable storage/SSA descriptors | A paused-value feature needs a new result/evaluation contract; [Decompiler](Systems/Decompiler.md) |
| Decompiler scheduling | One process engine worker, cooperative cancellation, bounded cache/work budget | Do not create extra concurrent Ghidra instances or hand engine pointers to UI; [Decompiler](Systems/Decompiler.md) |
| Memory model | Synchronous captured reads; a locally accepted write is not necessarily remotely applied | Preserve write destination, capability checks and restore/session invalidation; [Memory/history](Systems/MemoryAndHistory.md) |
| History | Bounded queues/ring can skip frames; recorded memory is distinct from resumable state | Require reconstructible state blocks and the current restore contract before offering replay; [Memory/history](Systems/MemoryAndHistory.md) |
| Comparison | Full included regions and nonzero frame required; Sound RAM excluded | Partial captures are not all comparable; exclusion is intentional noise control; `InCompare`, `CaptureSnapshot` |
| Access/search results | Bounded access rows, selected-byte RAM search hit lists and PRS scan budgets | Surface truncation/skipped work rather than interpreting missing results as absence; memory and disc subsystem pages |
| Disc reading/building | ISO primary tree only; CUE browsing narrower than rebuild parsing; rebuild assigns fresh LBAs | Game-specific sector assumptions can require additional work; [Disc/patching](Systems/DiscAndPatching.md) |
| Patch application | Script launch is not completion; generated patch script has no rollback after a write-time failure | Distinguish successful launch, validation and completed publication; `PatchLibrary::EmitPython` |
| AI investigation | M0 deterministic contracts only, no App collectors/UI/package I/O/action application | Later milestone plans are not callable implementation; [AI contracts](Systems/AIInvestigation.md) |
| Browser | Default build is viewer-only, decompiler off; optional web live requires special host/proxy | Check CMake options and actual platform support, not just shared source presence; [Dependencies](DEPENDENCIES.md) |

## Existing text that conflicts with current implementation

These discrepancies are documented here rather than rewriting broader historical/design documents.

- [ARCHITECTURE.md](../ARCHITECTURE.md) describes a host GPU geometry path for the 3D view.
  The bundled frontend actually calls `se_render_3d`, receives software RGBA, and uploads it.
- [BUILD.md](../BUILD.md) and the live-driver header contain native-only live descriptions.
  [CMakeLists.txt](../../CMakeLists.txt) has optional `SE_WEB_LIVE`; the default web build remains viewer-only.
  CMake's opening comment also predates the non-Windows frontend targets.
- The [Mednafen README](../../Integration/Mednafen/README.md) initialization table describes
  game-load/game-close hooks; current glue initializes lazily in `SeMednafenFrameHook`.
  A server-gate comment says restored state pauses, while `SeStateAfterRestore` resumes it.
  See [StateSources](Systems/StateSources.md) for exact source references.
- Introductory wording in `Sh2Decompiler.h` still describes an earlier PC-relative delay-slot
  limitation; current patches/semantic tests support several such cases. An earlier comment near
  raw `register0x` handling describes failure where current executable handling preserves text
  and withholds the signature. Read [Decompiler](Systems/Decompiler.md) for precise current rules.
- `ExecutionActions.h` mentions an older helper name (`ImplementedEffects`); current normalization
  uses `Normalize`. Follow implementation, not that stale name, when changing action effects.

## Not established by this investigation

This is source navigation and architecture mapping, not a correctness sign-off. No new hardware
or runtime defect is asserted based solely on the observations above.

- Native Windows/macOS UI, dialogs, GPU/audio device behavior and packaging were not executed.
- No real emulator game/rewind/restore run or independent hardware register audit was performed.
- Large synthetic decompiler suites, semantic oracles and historical review claims were located
  and inspected selectively, not exhaustively rerun or independently proven.
- `UpdateChecker` exposes a platform-outlives-request contract while allowing detached teardown;
  this pass did not establish all shutdown timing. This is an unresolved lifecycle question,
  not a confirmed defect. Evidence: `UpdateChecker.h`, `Start`, `Run`, destructor and entry points.
- Installer/update shell tooling, every emulator-specific accessor and full distribution/license
  compliance were not audited. Read their current integration documents before changes.
- Other projects in the RomHackingTools repository were outside this Saturn Explorer task.

[Investigation report](INVESTIGATION_REPORT.md) records validation and maintenance scope.
