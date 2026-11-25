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
| Profiler | Counters compiled into the patched emulator and unit-tested against the real patched `vdp1.cpp` / 68000 core / `sound.cpp` / SH-2 core (`Integration/Mednafen/check_component.py`, needs the checkout; this includes the bus-request counts of cache line fills and of the SH-2 DMAC, `component/BUS_EXPECTED.md`, whose ctest is skipped without a checkout) but not compared with a running commercial game (the purpose-built V1 guest matched its written-first expectations on one real run, 2026-11-25, via a headless recorder, not the Profiler panel; the F5/F9/F11 hook checks were not run there); the bus tests stub the devices behind the SCU, never address the cartridge (CS0 / CS1 / CS2 devices) and do not run `InitCommon`'s cache-mode assignment; VDP1 figure is command work (excludes the read/write slowdown hack and software `ENDR`); bus counts are bus-controller requests and depend on the cache mode; a manual SCU DSP step is not counted; self time only; busy time includes slow-bus waits; function ownership is conservative (after an interrupt or a lost observation the running function is charged to its raw-PC bucket until a return proves ownership again; an unconditional `bra` / `jmp @Rn` / `braf` to an address a call has entered is a tail call and is named, one into code the function already ran, or forward out of a function still inside its body, stays with it, and any other (a frameless function's first forward jump, a tail call to a function no call has entered yet) drops to the raw-PC bucket; a `bt` / `bf` tail call is not recognised; no exception-stack reconstruction); slave on-time and ownership are verified with synthetic streams and the real `SetActive` / `Reset`, not on a game; a profiler request made during a frame takes effect at the next frame boundary and the wire carries no "pending" indicator; function rows are retained keys (the 64 hottest of at most 3,072 tracked per frame), the rest is shown as Other per CPU, and a function that first appears after the 3,072nd key of a frame is Other for that frame; "Not observed" busy time on the Functions tab is an estimate from the frame counters; a window averages each v27-only quantity over the frames that carry it and says "(k of n frames)", and a per-CPU function filter uses only frames that carry the per-CPU Other split; a v26 and a v27 server's frames in one window show the two VDP1 figures separately, never blended; the Memory tab's "changed since sample" restarts on a source, emulator or timeline change and on scrubbing backward; the timeline is that of the capture's own bytes (the server stamps each snapshot, control block +56), so a lagging capture is never filed under the perf stream's newer timeline; an emulator built before that field leaves the capture's timeline unknown, and after a timeline change seen on the perf stream the tab samples nothing until 5 distinct captures have followed (the server's ring depth + 1); the panel has only been drawn headlessly | Treat absolute numbers as unverified until compared on a real title; [Profiler](Systems/Profiler.md) |
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

- `Integration/Mednafen/check_component.py` links with an ELF-only GNU ld option (`--unresolved-symbols=ignore-in-object-files`), so on MinGW/MSVC/macOS it exits 77 (skipped) up front; the Windows paths of `apply.py` / `check_migration.py` (CRLF, no symlink privilege) are covered on Linux by simulation only.
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
