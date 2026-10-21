# Memory inspection, search, frame history, and comparison

Inspected revision: `ff6a3b1fb1c54e4c0344613094ea65292b0023c2` (2026-10-20).
Evidence status: verified from source unless explicitly qualified.

Pseudocode inspection/navigation integration rechecked against `codex/decompiler-variable-inspection` (2026-10-20).

## Purpose and responsibilities

This frontend subsystem presents captured Saturn memory, edits appropriate targets, finds numeric values or byte sequences, records recent live frames, restores earlier emulator states, and compares two immutable frame snapshots. It is separate from disc-file search and persistent ROM patching. The [debugger](Debugger.md) uses its memory backend for watches, assembly and call-stack reconstruction; graphics edits also pass through the same write policy.

## Source locations and important types

| Responsibility | Source and symbols |
|---|---|
| Shared memory access | [Debug/MemoryBackend.h](../../../FrontEnd/src/Debug/MemoryBackend.h), [implementation](../../../FrontEnd/src/Debug/MemoryBackend.cpp): `IMemoryBackend`, `ContextBackend`, `MemoryReadRequest`, `MemoryReadResult`, `ReadRegionBytes` |
| Region vocabulary | [SaturnRegions.h](../../../FrontEnd/src/SaturnRegions.h); [Debug/MemoryCompare.h](../../../FrontEnd/src/Debug/MemoryCompare.h): `RegionId`, `RegionTraits`, `RegionRef`, `AddressSpace`, `RegionCaps` |
| Memory UI | [HexEditorPanel.cpp](../../../FrontEnd/src/HexEditorPanel.cpp): `HexEditorPanel::Draw`, `GoTo`, `CancelEdit`, `FindSelectionInRam`, `TakeBreakpointRequest`, `TakeLocateRequest` |
| Numeric and byte search | [Debug/MemorySearch.h](../../../FrontEnd/src/Debug/MemorySearch.h), [implementation](../../../FrontEnd/src/Debug/MemorySearch.cpp): `MemorySearch`, `SearchSnapshot`, `SearchHit`; [Debug/MemorySearchRunner.cpp](../../../FrontEnd/src/Debug/MemorySearchRunner.cpp): `StartFirst`, `StartNext`, `Poll`, `Stop`; [Debug/BytePatternSearch.cpp](../../../FrontEnd/src/Debug/BytePatternSearch.cpp) |
| Accessor history | [Debug/AccessLog.cpp](../../../FrontEnd/src/Debug/AccessLog.cpp): `AccessLog::Record`; [App.cpp](../../../FrontEnd/src/App.cpp): `RecordAccess`, `DrawAccessLog` |
| Rolling recording | [FrameRecorder.h](../../../FrontEnd/src/FrameRecorder.h), [implementation](../../../FrontEnd/src/FrameRecorder.cpp): `FrameRecorder`, `Frame`, `Scratch`, `Capture`, `Worker`, `SelectFrame`, `AttachStateBlock`, `ReconstructState`; [FrameLz.cpp](../../../FrontEnd/src/FrameLz.cpp) |
| Timeline identity / edits | [ScrubState.h](../../../FrontEnd/src/ScrubState.h): `PlanScrub`, `StagedEdits`, `EncodeEditBlob`; [App.cpp](../../../FrontEnd/src/App.cpp): `RefreshScrubContext`, `RecordPendingEdit`, `PlayFromScrubbedFrame`, `OnStateBlock` |
| Save slots | [SavestateSlots.h](../../../FrontEnd/src/SavestateSlots.h), [implementation](../../../FrontEnd/src/SavestateSlots.cpp); App's save/load/restore helpers |
| Snapshot/diff | [Debug/MemoryCompare.cpp](../../../FrontEnd/src/Debug/MemoryCompare.cpp): `CaptureSnapshot`, `Diff`, `BuildRows`, `CsvExport`; [CompareMarkers.h](../../../FrontEnd/src/CompareMarkers.h): `CompareMarkers`; [MemoryComparePanel.cpp](../../../FrontEnd/src/MemoryComparePanel.cpp) |
| External diff | [DiffTool.cpp](../../../FrontEnd/src/DiffTool.cpp): `BuildDiffArgv`, `WriteSnapshotFolder`, `PurgeDiffFolders`; `App::LaunchExternalDiff`; [Platform/IPlatform.h](../../../FrontEnd/src/Platform/IPlatform.h): `LaunchTool` |

## Entry points and execution flows

### Viewing and editing memory

`App::DrawHexEditor` → `HexEditorPanel::Draw` → `IMemoryBackend::ReadMemoryBatch` → `ContextBackend::ReadOne` → captured core memory (`se_read_vram`) or VDP register getters. Reads are synchronous local snapshot copies, not per-widget socket requests. `ReadOne` folds CPU mirrors to the low 27 bits, limits a request to 64 KiB, requires it to remain within a captured region and returns explicit unavailable/unmapped errors. `ReadRegionBytes` chunks larger requests and clears the whole output on failure.

Editing follows `CanWrite`/`WriteMemory`/`WriteRefusal`. `ContextBackend::WriteMemory` rechecks current policy at commit time; an edit allowed when it began may be forbidden when it ends. RAM/VRAM edits use the core write path; VDP register bytes rebuild 16-bit values through register setters. `WriteDestination` distinguishes snapshot-only, queued emulator and scrub-staged edits. App sets that destination explicitly; the presence of a write sink alone does not imply a live emulator.

`SourceId` combines context identity and generation. `NoteSourceChanged` is required when the same context starts serving another source/frame. The Hex Editor cancels its in-progress edit and caches when this changes. Its context menus return requests for breakpoints, data-file search or patch location; App routes them to the appropriate systems.

### Searching memory and recording accessors

`App::DrawRamSearch` starts `MemorySearchRunner::StartFirst` or `StartNext`; the calling thread captures bytes through the backend, then a worker scans its own snapshot and previous hits. `Poll` joins and installs the completed result once. A non-pthread web build instead runs time-budgeted slices in `Poll`. `Stop` joins/drops pending work; callers reset a search whose hit list was moved to the worker.

The Memory panel's selected-byte search is a different operation, implemented with `BytePatternSearch` over full region buffers so matches crossing read chunks survive. Results are capped and unreadable regions/extra matches are disclosed. Numeric narrowing reuses the prior candidate set rather than starting a fresh byte search.

“Find what accesses” is driven by logging data watchpoints, not by scanning historical memory. App's halt handler calls `RecordAccess`, decodes the accessor once, builds its call stack, records it through `AccessLog::Record`, then resumes when policy allows. Rows deduplicate by `(cpu, pc)`, keeping the latest stack and a hit count. New rows beyond the configured cap increment `Dropped`; existing rows continue updating.

### Recording and scrubbing

`App::BuildUI` records while playing live, rather than while paused, scrubbing or during an unresolved restore before its timeout. `FrameRecorder::Capture` copies raw snapshot regions/registers on the UI thread; `Worker` compresses them off-thread and publishes into a ring bounded by count and compressed bytes. The bounded raw queue drops work rather than blocking UI. `Clear` advances a generation so a frame already being compressed cannot repopulate a cleared session.

`RefreshScrubContext` asks `PlanScrub` whether to retain the shown frame or select another. `SelectFrame` resolves a frame number and decompresses under one ring lock. `Select` supplies `se_data_source` callbacks over recorder scratch; App creates/reuses `mScrubContext` and calls `se_begin_frame`. Panels therefore read an ordinary core snapshot of historical memory.

An index moves when the ring evicts from its front; stable navigation and staged edits use **frame numbers**, not retained indices. Decompression failure refuses the frame, clears the shown identity and falls back to live, rather than displaying invented zeros.

### Play From Here and save-state blocks

The emulator's state stream arrives separately from recorded memory. `App::OnStateBlock` rejects blocks older than its restore epoch floor, then hands blocks to both `FrameRecorder::AttachStateBlock` and the save-slot tracker. Recorder state is optional and can arrive before or after its frame; waiting deltas are bounded, while keyframes are stored independently because their own frame may never have entered the frontend ring.

`PlayFromScrubbedFrame` verifies reconstruction/capabilities, reconstructs the selected full savestate, encodes only staged edits belonging to that frame and calls `se_load_state`. On accepted submission it truncates abandoned future history, detaches affected comparisons, advances decompiler timeline identity, clears staged edits, leaves scrubbing and waits for asynchronous restore confirmation. Submission success is not proof that the emulator already restored it. See [StateSources.md](StateSources.md) for driver acknowledgements and server-side application.

### Frame comparison and export

`App::MarkCompareFrame` names the **displayed** frame, refuses unresolved restores and unapplied scrub edits, and calls `CaptureSnapshot` with source/derive guards. This deep-copies every included region completely or produces no snapshot. `CompareMarkers::Set` checks session/epoch membership; `App::OpenCompare` calls `Diff` and opens the panel.

The diff owns `shared_ptr<const MemSnapshot>` inputs, so ring eviction cannot alter it. A session change clears markers; a timeline change may detach navigation while preserving valid bytes for comparison. `Diff` checks sessions and region shapes; it does not require equal timeline epochs. `RegionRef` retains address-space identity, and traits control which actions are meaningful (for example, an app-derived framebuffer is not an SH-2 bus write target).

`CsvExport::Step` emits complete changed-byte output over UI frames and checks totals. External diff exports the same snapshot data to unique A/B directories with one binary file per included region, then `IPlatform::LaunchTool` receives `BuildDiffArgv` output. Argument splitting precedes literal `{a}`/`{b}` substitution; there is no shell expansion. Comparison folders survive application exit and are retained/purged using `PurgeDiffFolders` rules, preserving already-open diff inputs.

## State, lifetime, and interactions

`ContextBackend` holds a pointer to App's current context slot, so switching live/scrub/static views does not require reconnecting every panel. The backend itself does not own the context. Recorder scratch outlives its source callbacks and is replaced by the next selection; the core copies it during capture. Worker queue/ring mutexes and generation checks separate ownership; UI scratch/edit sink state belongs to the UI thread. Comparison snapshots own independent copies and may outlive either context or recorded frame.

`StagedEdits` owns one frame-tagged ordered poke batch. A write on a different frame clears the old batch, and retaining the same edited scrub context must retain its edits. Reconstructing from recording clears edits no longer visible. Save slots receive streamed state independently of whether recording is enabled, via `OnStateBlock`. `SavestateSlots::Latest` reconstructs the latest accepted keyframe/delta; `App::DoSaveState` calls `SaveToSlot` to write a versioned `.sestate` under the config states directory. `App::DoLoadState` calls `LoadFromSlot`, checks live restore capabilities, submits `se_load_state` without scrub edits, drops recorded history and begins the restore wait. Native emulator slot actions (`DoLoadEmulatorState` / `DoSaveEmulatorState`) are a separate App path.

Pseudocode inspection is a deliberately narrower memory consumer: `CapturedValueInspection`
reads only the panel's immutable matched snapshot, never `ContextBackend`. Captured globals and
pointer targets may open Memory only for complete supported WRAM locations; aliases are folded
before `HexEditorPanel::GoTo`. A loaded BIOS image can be previewed from the snapshot but is not
added to the Memory backend. Fixed-address Watch requests reuse the existing Watch refresh and
change-highlighting behavior after insertion; they are not stored snapshot-expression values.

## Extension points and reusable abstractions

- Add a new memory consumer behind `IMemoryBackend`; reuse `ReadRegionBytes`, rather than treating missing data as zeros or making UI code talk directly to an emulator.
- Add region support consistently in backend mapping, `SaturnRegions`, compare traits and region-capability decisions. Watchpoint support and navigation are not implied by a numerical address alone.
- Extend editing through the shared destination/refusal policy and source invalidation, including graphics table edits; reuse `SourceId` when holding an editor across frames.
- Add numeric search behavior in `MemorySearch`, scheduling in `MemorySearchRunner`, and UI in `App::DrawRamSearch`. Workers must not read App's mutable backend directly.
- Extend rewind metadata on `FrameRecorder::Frame` and the raw/copy/select paths together. Extend restore behavior only with a clear server-capability and acknowledgement contract.
- Add diff formats against immutable snapshots or `DiffResult`; reuse `RegionRef` and traits for context-menu actions. Keep complete export independent of virtualized visible rows.

## Constraints and confirmed limitations

- `ContextBackend` is synchronous and snapshot-based; an asynchronous remote reader would need a new request/response contract, not merely another implementation of the current return signature.
- VDP register edits are snapshot-only and App makes those windows read-only for live/scrub sources. CRAM and framebuffer emulator/replay edits require the peer VDP writer capability. A scrub source without resumable restore is also constrained by App's edit policy.
- Recording memory alone does not make a frame resumable. `CanReconstruct` requires a valid state block and its base keyframe; `GetStateStats` distinguishes absent, waiting, invalid and reconstructible state.
- Frame 0 and non-advancing numbers are not recorded. Queue pressure may skip frames. This is a bounded recent-history view, not a guarantee of every emulated frame (`Capture`).
- Sound RAM is deliberately excluded from frame comparisons by `InCompare`, while retaining its region identity for other tools. Its constant audio activity would otherwise dominate differences.
- The comparison capture requires all included memory regions and a known nonzero frame; it refuses partial captures. It therefore does not represent every possible partial savestate.
- Access logging is bounded (default 4096 rows, 32 stack frames per row), and selected-byte RAM search lists at most 500 hits. These are explicit reporting limits, not evidence that a larger result does not exist.

## Testing and verification

From `SaturnExplorer/`, build according to [BUILD.md](../../../BUILD.md), then run registered tests:

```sh
ctest --test-dir build --output-on-failure -R 'MemoryBackend|HexEditorPanel|MemorySearch|BytePatternSearch|MemoryCompare|CompareMarkers|DiffTool|ScrubState|FrameRecorder|RewindPipeline|SavestateSlots|MemoryRoundTrip|AccessLog|LivePoke|LiveRestoreSignal'
```

Start with [MemoryBackendTests.cpp](../../../FrontEnd/tests/MemoryBackendTests.cpp), [HexEditorPanelTests.cpp](../../../FrontEnd/tests/HexEditorPanelTests.cpp), [MemorySearchRunnerTests.cpp](../../../FrontEnd/tests/MemorySearchRunnerTests.cpp), [MemoryCompareTests.cpp](../../../FrontEnd/tests/MemoryCompareTests.cpp), [MemoryComparePanelTests.cpp](../../../FrontEnd/tests/MemoryComparePanelTests.cpp), [CompareMarkersTests.cpp](../../../FrontEnd/tests/CompareMarkersTests.cpp), [DiffToolTests.cpp](../../../FrontEnd/tests/DiffToolTests.cpp), [ScrubStateTests.cpp](../../../FrontEnd/tests/ScrubStateTests.cpp), [FrameRecorderTests.cpp](../../../FrontEnd/tests/FrameRecorderTests.cpp), and [RewindPipelineTests.cpp](../../../FrontEnd/tests/RewindPipelineTests.cpp). The latter exercises exporter → driver → recorder. Platform-specific and integration target availability follows [CMakeLists.txt](../../../CMakeLists.txt).

Coverage: backend, App routing, recorder, compare and worker ownership were inspected. This pass did not independently execute a real emulator restore, verify every save-slot operation or visually exercise every Memory panel action on Windows/macOS.

## Related documentation

[Memory Compare design index](../../MemoryCompare/README.md), [Mednafen rewind integration](../../../Integration/Mednafen/README.md), [debugger](Debugger.md), [state sources](StateSources.md), [feature map](../FEATURE_MAP.md).
