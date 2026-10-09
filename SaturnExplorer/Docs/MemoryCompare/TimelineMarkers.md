# Timeline markers and snapshot capture

The only permanent UI for [Frame Memory Compare](README.md). Lives in the transport bar
(`App::DrawTransportBar`, `App.cpp`, under the VDP preview) plus a small piece of state on `App`.
The capture rules it relies on are in
[SnapshotAndDiffEngine.md](SnapshotAndDiffEngine.md#capture-consistency).

## State

```cpp
struct CompareMarker {
    bool                               set = false;
    bool                               attached = false;   // frame still locatable on the timeline
    std::shared_ptr<const MemSnapshot> snap;               // owned copy; origin.frameNo is the key
};
CompareMarker markerA, markerB;
uint64_t      mCompareSession = 1;   // bumped when the emulator run / source changes
uint64_t      mTimelineEpoch  = 1;   // bumped when the recorded history is replaced
```

A marker is keyed by frame number, never by slider index (indexes shift as the ring evicts), and
owns its snapshot, so ring eviction cannot change or invalidate it.

## Marking

Available only while paused, like the scrub slider.

1. **Preconditions** (a disabled menu item with a tooltip says which fails):
   - a source is loaded and `mMemBackend.Connected()`;
   - not mid-restore: `mRestoreOutstanding == 0` and `!mRestoreUnconfirmable`;
   - when scrubbing: `mScrubShownIndex >= 0`, i.e. the last `Select` was not refused, and the shown
     frame has **no pending edits** (`mPendingEdits` empty or `mPendingEditsFrame !=
     mScrubShownIndex`). Pending edits change what the context serves without being part of the
     recorded frame, so the capture is refused rather than silently including them.
2. **Which frame:** the frame the *displayed context* holds, not where the slider points. A
   transport action earlier in the same UI frame can already have moved `mScrubIndex` while panels
   still draw the old frame; `RecordPendingEdit` documents the same hazard. The frame number is
   `FrameRecorder::SelectedFrameNumber()` when scrubbing, and `se_frame_number(mLiveCtx)`
   (the capture's frame) at the live head. See the engine doc for why neither can be re-derived from
   an index afterwards.
3. **Capture** runs synchronously, on the UI thread, through `mMemBackend` while `ScopedContextSwap`
   is active, so it reads the context the user is looking at. Any failure (a region fails to read,
   `se_derive_serial` or the source id moves, frame number 0) leaves the marker unchanged and shows
   a short inline reason. A marker is therefore always backed by a complete snapshot.
4. The snapshot is stamped with `{mCompareSession, mTimelineEpoch, frameNo, liveHead}`.
5. Marking A and B at the same frame is allowed; the diff is empty and says so.
6. **Clear Compare Markers** drops both snapshots.

## Identity lifecycle

Two counters, because two different things can be replaced. They must be bumped in the places the
app already handles those events, not detected by a new mechanism.

| Event | Where it is handled today | Effect on markers |
|---|---|---|
| Emulator restarted, replaced by another process, or its connection replaced (reconnect, new game or ROM launched from the Session menu) | `AdoptNewEmulatorInstance` (`se_live_captured_generation` changed) | `mCompareSession++`; **both markers cleared** |
| Source unloaded, a different state/dump/disc loaded, disconnect | the `se_destroy(mContext)` / `mContext = ...` paths in `App.cpp` (teardown near `:482`, loaders near `:1051`-`:1247`) | `mCompareSession++`; both cleared |
| Rewind history replaced: load-state jump, `DropRecordedHistory`, `FrameRecorder::Clear` | `DropRecordedHistory` | `mTimelineEpoch++`; markers become **detached** (kept) |
| Play From Here (`TruncateAfter(K)`) | `PlayFromScrubbedFrame` | `mTimelineEpoch++`; markers with `frameNo <= K` stay attached; with `frameNo > K` become detached. Frame numbers above K will be reused by the new timeline with different content, so a stale marker must never be located by number. |
| Recording length changed (ring reconfigured) | Recording Settings modal | none; eviction is handled by locatability below |

The protocol does not identify the ROM, so a ROM change is observable only as a new emulator process
or a new loaded source, both covered by `mCompareSession`. Use the same `mLiveConnGeneration`
bookkeeping as `AdoptNewEmulatorInstance`; do not add a second detector. `Diff` independently
refuses mismatched `sessionId`, so a missed bump cannot produce a mixed comparison in the engine,
only a stale marker in the UI.

**Attached** means the marker's epoch matches and the ring holds a frame whose
`FrameNumber(i) == frameNo` exactly (binary search; never "nearest"). **Detached** markers keep
their snapshot, still compare, and can't be located or scrubbed to.

## Drawing

- Right-click menu on the scrub slider (`ImGui::BeginPopupContextItem` on `##tp_scrub`): Set as
  Compare Frame A, Set as Compare Frame B, a separator, Compare A <-> B (disabled until both are
  set), Clear Compare Markers (disabled when none are set).
- Attached markers are drawn with the window draw list over the slider's frame rect, using the same
  index-to-x mapping as the slider (index `0..n-1` across the rect minus the grab width).
- Detached markers are drawn hollow, pinned at the left edge, with a tooltip ("not in rewind
  history; snapshot kept").
- Labels beneath the slider: `Frame A: 1800`, `Frame B: 1884`, plus a **Compare Memory...** button
  enabled only when both markers are set.
- Colours: A blue, B orange. Use theme accents if the theme already has fitting ones.

## Opening the panel

The button and the menu item do the same thing: run `Diff(markerA.snap, markerB.snap)`, hand the
result to the panel, set `mPanels.memoryCompare = true` and focus the window. Registration is listed
in [ComparePanel.md](ComparePanel.md#registration-done-in-the-hook-up-pass). A `Diff` error
(session mismatch) shows inline and opens nothing.

## Tests

The transport bar is part of `App` and is not unit-testable in isolation. Coverage comes from:

- engine tests for capture, immutability, identity and truncation (see the engine doc)
- pure helpers, unit-tested: frame-number to slider-x mapping (empty ring, single frame, exact-match
  location, evicted frame) and the epoch/attached rules (including `TruncateAfter(K)` on both sides
  of K)
- a recorder test for `SelectedFrameNumber()`: tracks the decompressed frame after eviction shifts
  indexes, and is 0 after a refused `Select`
- a `PanelInteractionTests`-style headless test of the context menu if `ImGuiHarness` can host the
  slider; otherwise an entry in `Docs/FunctionalityVerification/` for manual checks of: marking at
  the live head while paused, marking a scrubbed frame, marking refused with pending edits,
  emulator restart, and Play From Here
