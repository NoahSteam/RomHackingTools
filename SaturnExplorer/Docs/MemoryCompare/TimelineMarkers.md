# Timeline markers and snapshot capture

The only permanent UI for [Frame Memory Compare](README.md). Lives in the transport bar
(`App::DrawTransportBar`, `App.cpp`), under the VDP preview, plus a small piece of state on `App`.

## State

A `CompareState` member on `App` (a tiny struct, not a new subsystem):

```cpp
struct CompareMarker {
    bool                                 set = false;
    uint64_t                             frameNo = 0;
    std::shared_ptr<const MemSnapshot>   snap;   // owned copy, see SnapshotAndDiffEngine.md
};
CompareMarker markerA, markerB;
```

The marker owns its snapshot, so it stays valid when the rewind ring evicts the frame. It is keyed
by frame number, not by slider index, because slider indexes shift as the ring advances.

## Marking

- Available only while paused, like the scrub slider itself.
- **Set as Compare Frame A/B** captures the snapshot of the frame currently shown: it calls
  `CaptureSnapshot(mMemBackend, frameNo)` while the scrub context (or the live context when paused
  at the head) is active. The frame number is `FrameRecorder::FrameNumber(mScrubShownIndex)` when
  scrubbing, otherwise the live frame number.
- If capture fails (a region would not decode), the marker is not set and a short inline message
  says why. The rest of the state is untouched.
- Setting A and B to the same frame is allowed; the result is an empty diff and says so.
- **Clear Compare Markers** drops both snapshots.

## Drawing

- Right-click menu on the scrub slider (`ImGui::BeginPopupContextItem` on `##tp_scrub`):
  Set as Compare Frame A, Set as Compare Frame B, a separator, Compare A <-> B (disabled until both
  are set), Clear Compare Markers (disabled when none are set).
- Markers are drawn with the window draw list over the slider, using the slider's frame rect. The
  slider maps index `0..n-1` across the rect minus the grab width, so a marker's x is that same
  mapping applied to the index found for its frame number. A binary search over
  `FrameNumber(i)` is enough; frame numbers are increasing.
- A marker whose frame is no longer in the ring is drawn pinned at the left edge with a hollow
  style and a tooltip ("frame evicted; snapshot kept"). The comparison still works.
- Labels beneath the slider: `Frame A: 1800`, `Frame B: 1884`, in the marker colours, next to a
  **Compare Memory...** button that is enabled only when both markers are set.
- Colours: A blue, B orange, taken from the existing theme where one fits; do not hard-code new
  palette entries if the theme already has accents.

## Opening the panel

The button and the menu item both do the same thing: build the diff from the two snapshots, hand it
to the panel, set `mPanels.memoryCompare = true`, and focus the window. Registration of the panel is
listed in [ComparePanel.md](ComparePanel.md).

## Interactions to check

- Scrubbing, Play From Here and `TruncateAfter` change the ring but must not touch markers.
- Disconnecting or loading a different state: clear both markers, since the snapshots are from a
  different session.
- Changing the recording length (Recording Settings) reconfigures the ring; markers are unaffected.
- Pending pokes made while scrubbed are not part of the recorded frame. Confirm what the backend
  serves at capture time and say in the panel header whether a snapshot includes pending edits.

## Tests

The transport bar is part of `App` and is not unit-testable in isolation. Coverage comes from:

- engine tests for capture/immutability (see SnapshotAndDiffEngine.md)
- a pure helper for the frame-number to slider-x mapping and its edge cases (empty ring, single
  frame, evicted frame), unit-tested separately
- a `PanelInteractionTests`-style headless test for the context menu if `ImGuiHarness` can host the
  slider; otherwise a manual verification entry added to `Docs/FunctionalityVerification/`
