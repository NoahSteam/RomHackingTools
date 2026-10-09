# Frame Memory Compare

Status: **designed, not implemented.** This directory is the spec the implementation is built
against. Component docs:

| Component | Doc | New code |
|---|---|---|
| Snapshot + diff engine | [SnapshotAndDiffEngine.md](SnapshotAndDiffEngine.md) | `FrontEnd/src/Debug/MemoryCompare.{h,cpp}` |
| Timeline markers + snapshot capture | [TimelineMarkers.md](TimelineMarkers.md) | `App::DrawTransportBar`, a small `CompareState` |
| Memory Compare tab | [ComparePanel.md](ComparePanel.md) | `FrontEnd/src/MemoryComparePanel.{h,cpp}` |

## What it is

Pick two frames on the rewind timeline, then see exactly which bytes of emulator memory differ
between them, grouped by region, in a side-by-side hex view. It answers "what changed in RAM
between the moment before I pressed the button and the moment after?", which is the first step of
most reverse-engineering sessions. From a changed byte the user can jump to the existing Memory,
Assembly, Watch and Breakpoint tools.

## Workflow

1. Pause and scrub to the frame to use as the baseline. Right-click the timeline, **Set as
   Compare Frame A**.
2. Scrub to another frame, **Set as Compare Frame B**.
3. Click **Compare Memory...** (or right-click, **Compare A <-> B**). The Memory Compare tab opens
   and is focused.
4. Pick a region in the summary table, tick **Changes Only**, and inspect the diff.
5. Right-click a changed byte for Go to Memory, Add Watch, Break on Write, View in Assembly or
   Export.

## Design decisions

- **A feature of the rewind timeline, not a new toolbar.** The only permanent UI is two markers and
  one button in the transport bar. The tab is dockable, lives next to Memory / SH-2 Assembly, and
  costs nothing when closed.
- **Snapshots are owned copies.** Marking a frame copies its memory out. A marker never refers to a
  rewind-ring slot, so ring eviction cannot silently change or invalidate a comparison. A snapshot
  is about 3.5 MB (see the engine doc), so holding two is cheap.
- **Snapshot diff only.** A byte that changed and changed back between A and B does not appear. The
  UI says so, and points at the Access Log ("find what accesses") for write history rather than
  duplicating it.
- **Only captured regions.** The rewind recorder captures LWRAM, HWRAM, Sound RAM, VDP1 RAM,
  VDP1 frame buffer, VDP2 RAM, CRAM and the VDP1/VDP2 register images. BIOS and cartridge space are
  not captured, so they are not in the summary and "Find in ROM" is not offered.
- **No emulator or protocol change.** Everything reads through `IMemoryBackend`, which already
  follows the live context or the scrubbed frame.
- **Pure logic is separate from ImGui.** The engine has no UI dependency and is unit-tested. The
  panel is its own class (like `HexEditorPanel`) and is tested headlessly with `ImGuiHarness`.
  `App.cpp` only gains registration and glue.

## Differences from the concept image

- The frame cards have no VDP thumbnails in v1. They need a per-snapshot composited render; this is
  a possible later addition.
- The region table lists the regions we capture, including VDP1 FB and register images, and has no
  BIOS row.
- There is a single hex table with both columns inside it, so "synchronised scrolling" is free
  rather than a feature to build.
- Time is shown as `frame / 60` (NTSC). PAL titles will read slightly off until the video standard
  is plumbed through; the label is a hint, the frame number is authoritative.

## Build order and parallelism

1. **Serial, small:** write `MemoryCompare.h` (types and signatures only). It is the contract the
   three pieces share.
2. **Parallel:** engine + tests; panel + headless tests against hand-built snapshots; timeline
   markers + capture. The panel is its own class so only the timeline work edits `App.cpp` at this
   stage. `CMakeLists.txt` gets one line from each.
3. **Serial hook-up:** register the panel (`Panels` flag, `PanelList()`, `BuildUI`,
   `BuildDefaultLayout`, `AdoptNewPanels`), connect the button to capture, diff and panel, and wire
   the context-menu actions.

## Later (not in v1)

- **Compare Against A** as a persistent mode: B follows the current scrub frame and the diff
  re-runs when the index changes. A diff is a few milliseconds, so it can run synchronously.
- Shift-JIS text column; thumbnails on the frame cards.
- Optionally pin more than two snapshots.

## Known limits and risks

- Rewind frames the front end never recorded cannot be marked. The recorder drops frames when the
  compressor is behind, so marker frame numbers can be sparse.
- A frame whose regions fail to decode is refused by `FrameRecorder::Select`; marking it must fail
  visibly ("Snapshot: Invalid"), never produce a zero-filled snapshot.
- VDP1 FB and VDP2 VRAM can change in thousands of places per frame. The engine caps stored ranges
  and the UI shows "N+" rather than building millions of rows.
- Pending pokes made while scrubbed (`App::OnScrubEdit`) are not part of the recorded frame. The
  capture step must be checked to confirm whether the backend serves them, and the behaviour
  documented in the tab.
