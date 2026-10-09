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
- **Snapshots are consistent and identified.** Every region of a snapshot comes from one frame of
  one session. The core already copies all regions into one pinned snapshot per context, so
  reading them back cannot tear, at the live head or on a scrubbed frame; capture also re-checks
  `se_derive_serial` and the source id and fails rather than keep a torn snapshot. Each snapshot is
  stamped with a session id and a timeline epoch so unrelated captures can never be compared
  and a marker can never be located on a timeline that replaced it. Details:
  [engine doc](SnapshotAndDiffEngine.md#capture-consistency) and
  [identity lifecycle](TimelineMarkers.md#identity-lifecycle).
- **Counts are exact, outputs are complete.** The stored range list is a capped display summary.
  The hex rows and the CSV export rescan the immutable snapshots, so no changed byte is ever
  omitted by a cap.
- **Regions are not just address ranges.** Every location is a `RegionRef` with an explicit region
  id, address space and capability set. VDP1 FB (derived image), the VDP register images and Sound
  RAM (68K-written) are not plain SH-2 bus memory, so navigation, watches, breakpoints and the
  assembly view are enabled per region rather than assumed.
- **Pending scrub edits are never captured.** Marking is refused while the shown frame has pending
  edits, because the context serves them without them being part of the recorded frame.
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

- There is no Refresh button: snapshots are immutable and the diff recomputes from them when an
  option changes (see the panel doc).
- The frame cards have no VDP thumbnails in v1. They need a per-snapshot composited render; this is
  a possible later addition.
- The region table lists the regions we capture, including VDP1 FB and register images, and has no
  BIOS row. Context-menu actions are enabled per region (for example, no Break on Write on Sound RAM
  or VDP1 FB).
- There is a single hex table with both columns inside it, so "synchronised scrolling" is free
  rather than a feature to build.
- Time is shown as `frame / 60` (NTSC). PAL titles will read slightly off until the video standard
  is plumbed through; the label is a hint, the frame number is authoritative.

## Build order and parallelism

1. **Serial, small:** write `MemoryCompare.h` (types and signatures only). It is the contract the
   three pieces share.
2. **Parallel:** engine + tests; panel + headless tests against hand-built snapshots; timeline
   markers + capture (including the small `FrameRecorder::SelectedFrameNumber()` accessor and its
   test). The panel is its own class so only the timeline work edits `App.cpp` at this
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
- A frame whose regions fail to decode is refused by `FrameRecorder::Select`; marking then fails
  with an inline reason and sets no marker. There is no "invalid snapshot" state to display, and a
  zero-filled snapshot is never produced.
- VDP1 FB and VDP2 VRAM can change in thousands of places per frame. Only the stored range list is
  capped; counts, rows and CSV are exact (CSV streams, since an all-bytes-differ export is millions
  of lines).
- `FrameRecorder` gains one small accessor, `SelectedFrameNumber()`, because the frame a scrubbed
  snapshot was taken from cannot safely be re-derived from a ring index.
- Nothing in the live protocol identifies the ROM. A ROM change is visible only as a new emulator
  process or a loaded source, both of which already reset session state in `App`.
