# Timeline markers and snapshot capture

The only permanent UI for [Frame Memory Compare](README.md). Lives in the transport bar
(`App::DrawTransportBar`, `App.cpp`, under the VDP preview) plus a small piece of state on `App`.
The capture rules it relies on are in
[SnapshotAndDiffEngine.md](SnapshotAndDiffEngine.md#capture-consistency).

## State

The marker state and the identity rules live in `FrontEnd/src/CompareMarkers.h`, a header-only class
with no ImGui, App or recorder dependency so the rules are unit-tested
(`CompareMarkersTests.cpp`). `App` holds one `CompareMarkers mCompare` (wired in by the App-integration change).

- `Set(slot, snapshot)` adopts a snapshot as marker A or B. It is **refused** unless the snapshot
  carries the current session and epoch (`Origin(frameNo, liveHead)` builds that stamp), so a capture
  begun before a reset can never become a marker after it.
- `NewSession()` bumps the session and drops both markers. `ReplaceTimeline()` bumps the epoch.
  `TruncateAfter(keptFrame)` bumps the epoch and keeps attached only the markers at or before the
  resume frame.
- `IndexOf(slot, count, frameAt)` finds a marker on the timeline: `-1` when it is detached (an older
  epoch) or its frame has left the ring; otherwise the exact index by binary search
  (`FindFrameIndex`), never a nearest match.
- `SliderGrabCenterX(...)` is where ImGui draws the grab of an integer `SliderInt`, so a marker is
  drawn exactly over its frame. It mirrors ImGui's `SliderBehaviorT`, which is not exposed, and the
  tests compare it with ImGui's own result so an ImGui update that moves the grab fails a test.

A marker is keyed by frame number, never by slider index (indexes shift as the ring evicts), and owns
its snapshot, so ring eviction cannot change or invalidate it.

## Marking

Available only while paused, like the scrub slider.

1. **Preconditions** (when one fails, nothing is marked and the reason appears in the row under the
   scrub bar):
   - a source is loaded and `mMemBackend.Connected()`;
   - not mid-restore: `mRestoreOutstanding == 0` and `!mRestoreUnconfirmable`;
   - when scrubbing: `mScrubShownIndex >= 0`, i.e. the last `Select` was not refused, and the shown
     frame has **no pending edits** (`mPendingEdits` empty or `mPendingEditsFrame !=
     mScrubShownIndex`). Pending edits change what the context serves without being part of the
     recorded frame, so the capture is refused rather than silently including them.
2. **Which frame:** the frame the *displayed context* holds, not where the slider points. A
   transport action earlier in the same UI frame can already have moved `mScrubIndex` while panels
   still draw the old frame; `RecordPendingEdit` documents the same hazard. The frame number is
   `FrameRecorder::SelectedFrameNumber()` when scrubbing, and `se_frame_number(mContext)` (the
   displayed context's captured frame) at the live head. See the engine doc for why neither can be re-derived from
   an index afterwards.
3. **Capture** runs synchronously, on the UI thread, through `mMemBackend`. It is only reachable from
   the transport bar, which `BuildUI` draws inside `ScopedContextSwap`, so it reads the context the
   user is looking at. Any failure (a region fails to read,
   `se_derive_serial` or the source id moves, frame number 0) leaves the marker unchanged and shows
   a short inline reason. A marker is therefore always backed by a complete snapshot.
4. The snapshot is stamped with `mCompare.Origin(frameNo, liveHead)` (the current session and epoch).
5. Marking A and B at the same frame is allowed; the diff is empty and says so.
6. **Clear Compare Markers** drops both snapshots.

## Identity lifecycle

Two counters, because two different things can be replaced. They must be bumped in the places the
app already handles those events, not detected by a new mechanism.

| Event | Where `App` already handles it | Effect on markers |
|---|---|---|
| Emulator restarted, replaced by another process, or its connection replaced (reconnect, new game or ROM launched from the Session menu) | `AdoptNewEmulatorInstance` (`se_live_captured_generation` changed) | `ResetCompareSession()` (`NewSession()` plus dropping the open comparison); **both markers cleared** |
| Source unloaded, a different state/dump/disc loaded, disconnect | `CloseData`, which every loader and the disconnect path go through before replacing `mContext` | `ResetCompareSession()`; both cleared |
| Rewind history replaced: load-state jump (an emulator or app slot), a fresh recording | `DropRecordedHistory` and `StartRecording`, both through `ClearRecordedFrames()` (which pairs `FrameRecorder::Clear` with the epoch bump) | `mCompare.ReplaceTimeline()`; markers become **detached** (kept) |
| Play From Here (`TruncateAfter(K)`) | `PlayFromScrubbedFrame` | `mCompare.TruncateAfter(K)`; markers with `frameNo <= K` keep their state (one already detached stays detached); with `frameNo > K` become detached. Frame numbers above K will be reused by the new timeline with different content, so a stale marker must never be located by number. |
| Recording length changed (ring reconfigured) | Recording Settings modal | none; eviction is handled by locatability below |

The protocol does not identify the ROM, so a ROM change is observable only as a new emulator process
or a new loaded source, both covered by the session. Use the same `mLiveConnGeneration`
bookkeeping as `AdoptNewEmulatorInstance`; do not add a second detector. `Diff` independently
refuses mismatched `sessionId`, so a missed bump cannot produce a mixed comparison in the engine,
only a stale marker in the UI.

**Attached** means the marker's epoch matches and the ring holds a frame whose
`FrameNumber(i) == frameNo` exactly (binary search; never "nearest"). **Detached** markers keep
their snapshot, still compare, and can't be located or scrubbed to.

## Drawing

- Right-click menu on the scrub slider (`ImGui::BeginPopupContextItem` on `##tp_scrub`): Set as
  Compare Frame A, Set as Compare Frame B, a separator, Compare A <-> B (disabled until both are
  set), Clear Compare Markers (disabled when none are set). The Set items are always enabled; a
  refused mark shows its reason in the row below rather than needing a hover.
- Attached markers are drawn with the window draw list over the slider's frame rect, using the same
  grab position as the slider (`SliderGrabCenterX`, given the slider's frame rect).
- Detached markers are drawn hollow and pinned at the left edge; the `(detached)` label in the row
  below carries the tooltip (the frame has left the history or was never recorded; the snapshot is
  kept).
- Labels beneath the slider: `Frame A: 1800`, `Frame B: 1884`, plus a **Compare Memory...** button
  enabled only when both markers are set, and the status line when a mark or compare was refused.
  The items wrap to the window width (`LineFlow`, in `CompareMarkers.h`), and the status wraps on a
  line of its own. The strip reserved for the transport bar is sized by `CompareRowHeight`, which runs
  the same `LineFlow` over the same widths, so what is reserved cannot drift from what is drawn.
- Colours: A blue, B orange. Use theme accents if the theme already has fitting ones.

## Navigating to a marked frame

Go to A / Go to B and an attached **Go to Memory** name the frame by number, never by index. The ring
evicts from the front, so an index looked up now can name a different frame by the time the scrub
context is built on the next UI frame (the worker may publish a frame and evict the oldest in
between). `ScrubToFrame(frameNo)` checks the frame is still there and sets `mScrubTargetFrame`;
`RefreshScrubContext` then calls `FrameRecorder::SelectFrame`, which finds the frame and decompresses
it under one recorder lock, and refuses (with a message in the compare row, falling back to the live
view) if it has been evicted. The "already showing this frame" shortcut compares the frame number
(`mScrubShownFrame`) as well as the index.

## Opening the panel

The button and the menu item do the same thing: run `Diff(mCompare.Snapshot(A), mCompare.Snapshot(B))`, hand the
result to the panel, set `mPanels.memoryCompare = true` and focus the window. Registration is listed
in [ComparePanel.md](ComparePanel.md#registration-done-in-the-app-integration-change). A `Diff` error
(session mismatch) shows inline and opens nothing.

## Tests

The transport bar is part of `App` and is not unit-testable in isolation. Coverage comes from:

- engine tests for capture, immutability, identity and truncation (see the engine doc)
- `CompareMarkersTests.cpp`: the session/epoch rules (including `TruncateAfter(K)` on both sides of
  K and a detached marker staying detached), exact frame lookup (sparse ring, evicted frame), and
  `SliderGrabCenterX` against ImGui's own `SliderBehavior`
- a recorder test for `SelectedFrameNumber()`: tracks the decompressed frame after eviction shifts
  indexes, and is 0 after a refused `Select`
- the transport bar and its context menu live in `App` and are checked by hand (an entry in
  `Docs/FunctionalityVerification/`): marking at the live head while paused, marking a scrubbed
  frame, marking refused with pending edits, emulator restart, and Play From Here
