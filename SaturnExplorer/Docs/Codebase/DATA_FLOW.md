# Significant execution paths

Inspected revision: `ff6a3b1fb1c54e4c0344613094ea65292b0023c2`.
The traces below name actual implementation entry points; linked subsystem pages carry details.

## Startup and shutdown

[WinMain.cpp](../../FrontEnd/Platforms/Windows/WinMain.cpp), `wWinMain`, or
[WebMain.cpp](../../FrontEnd/Platforms/Web/WebMain.cpp), `main`, initializes the platform first,
then `App::Initialize` in [App.cpp](../../FrontEnd/src/App.cpp). ImGui already exists when App
sets its layout filename. Preferences and annotation stores load, and desktop App starts
`DecompilerRunner::Start`. Native startup can enable live auto-connect. The native event loop
calls `BeginFrame → BuildUI → EndFrame`; browser execution uses an Emscripten callback.

On native exit, `App::Shutdown` stops search and decompiler work before saving settings/watch
session/comments/names and `CloseData`; the entry point then invokes platform shutdown.
See [Frontend](Systems/Frontend.md) and [StateSources](Systems/StateSources.md).

## Open a capture and derive a view

`App::OpenSavestate`/`OpenSavestateBuffer`/`OpenFullDump` creates a savestate data source.
`CreateContextFromSource` wraps it in `se_create`, then snapshots via `se_begin_frame`.
[HostAbi.cpp](../../Core/src/HostAbi.cpp) forwards to `Context::BeginFrame` in
[Context.h](../../Core/src/Context.h). `HardwareSnapshot` reads the available regions and
registers, `Vdp1Parser` walks commands, and `GeometryBuilder` derives scene geometry. Context
owns the result and relevant indexes/caches. Static files are not re-read every UI frame.
See [Graphics](Systems/Graphics.md) for exact derive operations and [StateSources](Systems/StateSources.md)
for formats, byte-order conversion, source ownership and missing-region behavior.

## Live emulator to synchronized panels

The emulator hook publishes a snapshot through [se_export.c](../../Integration/Common/se_export.c).
[LiveDriver.cpp](../../Drivers/Live/src/LiveDriver.cpp) receives snapshots on its transport thread.
App polls connection completion with `PollLiveOpen`. `BuildUI` calls `CaptureLiveFrame` when
running, settling after a step, halted with changed content, waiting for restore, or adopting a
new emulator session. The source capture pins one immutable remote snapshot across all core
reads; separate register and RAM queries therefore belong to one capture.

After capturing, `App::AdoptNewEmulatorInstance` reconciles session-local state. App synchronizes
breakpoints/tracepoints, drains trace events, polls stop identity and updates `StepHaltMachine`.
The identity includes more than a PC: repeated stops and replaced connections must be distinct.
Panel read models use the resulting context. See [Debugger](Systems/Debugger.md).

## Commands, recording, scrub and export order

`App::BuildUI` records eligible live frames with `FrameRecorder::Capture` and drains emulator
state blocks. `BuildTopBarViewModel` feeds toolbar/native menu/keyboard enablement.
`ExecuteTopBarCommand` runs only after rechecking `TopBarCommandEnabled` against current state.
A recorded-view context is then selected through `RefreshScrubContext`; the scoped swap routes
data panels to historical memory while transport operations retain access to the live context.

Data-menu saves queue flags/requests rather than exporting during dispatch. `DumpMemory`,
`SaveScreenshot`, layer exports and comparison exports run against the selected view.
`IPlatform::SaveFile → SaveOutcome → App::ReportSave` distinguishes saved/cancelled/failed.
The live context is restored at frame end. See [MemoryAndHistory](Systems/MemoryAndHistory.md)
for staged writes, rewind reconstruction, restore waits and compare-frame identity.

## Pseudocode worker and stale results

`App::UpdateDecompiler` chooses current view/function/CPU information for `DecompilerPanel`.
Capture construction, function discovery and `DecompilerRunner` submission operate on owned
`MemorySnapshot` bytes, not live emulator reads inside Ghidra. The worker creates/configures
`SaturnArchitecture`, uses `CaptureLoadImage` and `Sh2Decompiler`, and returns printed/model
results. The panel checks the completion against the current source, captured bytes, edits,
BIOS and user metadata before adopting it; hidden-panel and running-view rules matter.
See [Decompiler](Systems/Decompiler.md) for precise APIs and test families.

## Memory edits to emulator or recorded frame

The memory/debug panels use `ContextBackend` rather than reaching into emulator memory.
Its writes go through core snapshot/write APIs; the live source queues supported remote writes,
while historical edits route through the recorder's edit sink into staged edits. App reconciles
acknowledgements/capabilities and blocks writes across unresolved restore/session transitions.
An accepted local preview is not proof a remote write has completed. See
[MemoryAndHistory](Systems/MemoryAndHistory.md) and [StateSources](Systems/StateSources.md).

## Texture or code bytes to disc patch

`App::BuildTextureNeedle` or a selected byte range supplies a packed-byte needle to
`DataSearchRunner`. Search scans selected game-data roots (raw or PRS) and returns results to
the request's destination. Patch location acceptance is explicit; `AcceptLocateMatch` records
an association in `PatchLibrary`. `ApplyChangesToDisc` uses accepted locations/current edits;
`BuildDisc` calls the disc-building components and can launch the resulting image. A search hit
alone is not an approved memory-to-file mapping. See [DiscAndPatching](Systems/DiscAndPatching.md).

## Sound inspection to preview/export

`App::DrawSound → se_get_scsp_slots` reads captured SCSP state. `DecodeSlotSample →
se_decode_scsp_sample` produces mono PCM at its natural pitch. `PlaySound` resamples only when
needed for device rate limits; `ExportSound` wraps PCM in WAV and saves. `PlaySoundFrame` calls
`ScspMixVoices` for a static stereo reconstruction and `IPlatform::PlayAudio` for output. The
preview omits DSP and time evolution; see [Frontend](Systems/Frontend.md).

## AI investigation: current boundary

There is no application request/provider/action execution path yet. The implemented M0 module
validates evidence, JSON responses, canonical targets, fingerprints and action previews through
explicit interfaces and fixture-backed tests. Do not infer live workflow availability from the
plan or from source files being compiled into the desktop frontend. See
[AIInvestigation](Systems/AIInvestigation.md).
