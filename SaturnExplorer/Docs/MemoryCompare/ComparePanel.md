# Memory Compare panel

`FrontEnd/src/MemoryComparePanel.{h,cpp}`. A dockable tab, its own class like `HexEditorPanel`,
held by `App` as `mMemoryCompare`. Part of [Frame Memory Compare](README.md).

## Layout

```
Memory Compare
+----------------------------------------------------------------------+
| Frame A (baseline)  1800   00:30.000   | Frame B (compare)  1884 ...  |
+----------------------------------------------------------------------+
| [x] Changes Only  [x] Show Context (2 lines)  [Export...] [Go to A]   |
| [Go to B]  Jump to: [________] [Go]                                   |
+----------------------------------------------------------------------+
| Region        Size   Changed Bytes   Changed Ranges    <- summary     |
| All Memory    3.4 MiB      373              20                        |
| HWRAM         1 MiB        247              12                        |
| ...                                                                   |
+--------------------------------- (draggable splitter) ----------------+
| Address   Frame A (1800)    Frame B (1884)    ASCII (A)   ASCII (B)   |
| 06034F20  00 64 00 64 ...   00 5A 00 64 ...   .d.d...      .Z.d...    |
| ...                                                                   |
+----------------------------------------------------------------------+
```

The summary and the hex diff are stacked and split with `HorizontalSplitter` / `SplitTopHeight`
from `PanelWidgets.h`, not a hand-rolled splitter. That helper splits vertically, so the layout is
summary over diff rather than side by side. There is no region combo: selecting a row in the
summary is how a region is chosen.

## Inputs

The panel owns no memory and runs no diff. `App` gives it:

- a `DiffResult` from `Diff()`, which holds the two `shared_ptr<const MemSnapshot>` (A and B), or
  null before a comparison exists
- whether each side is still attached to the rewind timeline

(`Draw(const DiffResult*, bool aAttached, bool bAttached)`.) It rebuilds the visible line list
(`BuildRows` per region) when the selected region, Changes Only, or Show Context changes, and
never reads the emulator. A different pair of snapshots is recognised by `weak_ptr` identity, not
by address, and starts clean: selection, region and any `GoTo` still waiting all reset.

The split between the summary and the diff is not persisted (the Call Stack persists its split
through `Settings`). It resets to a default height on each launch; persisting it is a follow-up.

**There is no Refresh button.** The snapshots are immutable and the diff is deterministic, so
there is nothing to refresh: changing an option recomputes the rows from the same two snapshots
automatically. A control that could recapture memory would risk quietly comparing different frames
than the ones the user marked, so the panel has no way to recapture. Getting new data means setting
the markers again on the timeline. (The later live "Compare Against A" mode shows B as an explicit
`Live - frame N` card that visibly follows the timeline.)

## Header cards

Frame number, time (`frame / 60`, see README for the PAL caveat), and the snapshot's provenance:
`live head` or `rewind frame`, plus a `[detached]` badge when the frame is no longer on the rewind
timeline (tooltip: it is no longer in the rewind history; the snapshot is kept and still compares). There is deliberately no
"Snapshot: Valid/Invalid" line. A snapshot only exists if its capture fully succeeded, so showing
one is already the statement that it is complete; a failed capture is reported at the timeline when
the user tries to mark, and no card is created.

"Go to A / Go to B" scrub the timeline to that frame. They are disabled for a detached side, with
the tooltip saying why.

## Region summary table

One row per captured region plus an "All Memory" entry, columns Region, Size, Changed Bytes,
Changed Ranges. Counts are exact (`RegionDiff::changedBytes` / `rangeCount`); only the stored range
list is capped, and the cap does not affect the hex view or export, which rescan the snapshots. Rows
use `RowSelectable`; selecting one filters the hex diff. Regions with zero changes are shown dimmed
but stay selectable. The colour swatch beside a region follows its type (work RAM, VDP1, VDP2, colour RAM,
sound RAM).

## Hex diff

- A single ImGui table with columns: Address, Frame A hex (16 bytes), Frame B hex (16 bytes),
  ASCII A, ASCII B. One table means one scroll position, so A and B scroll together by
  construction; there is no synchronisation code.
- Virtual scrolling with `ImGuiListClipper` over the `DiffRow` list. Rows are fixed height.
- Changed bytes get a background on both sides (A and B each in a distinct tint), unchanged bytes
  in context rows are drawn normally, and context rows are dimmed relative to rows with changes.
  An elided run (`gapBefore`) draws a thin separator with the count of skipped rows.
- ASCII columns are plain ASCII, with non-printable bytes as `.`. Shift-JIS is a possible
  follow-up, shared with whatever `HexEditorPanel` does.
- Addresses are shown as the bus address (`06034F20`) for `Sh2Bus` regions. For `DeviceImage` and
  `RegisterImage` regions the row label is region-qualified (`VDP1 FB+1230`, `VDP2 Regs+E0`: hex offset, no padding),
  because those are not bus-addressed memory; the rest of the app keeps using the Memory tab's
  addresses for them. Selection and every action carry a `RegionRef`, not a raw address.
- **Jump to** box (`ParseLocation`): accepts a bus address (mirrors folded) or `Region+offset`, and
  scrolls to the row and selects the byte. When a single region is selected it follows the byte into
  its region; All Memory stays All Memory. If Changes Only elides the row, the panel switches Changes
  Only off for that jump and says so (once, not when there are no differences at all).
- Selection is keyed by `RegionRef`, so it survives a region switch or a row-list rebuild, and is
  cleared when a different pair of snapshots is shown. Click selects a byte, shift-click extends
  within the region.
- Read-only. There is no editing here; the Memory tab is for that.

## Context menu actions

Right-click a byte. The panel does not perform the action. Like `HexEditorPanel`, it raises a
request carrying the action, a `RegionRef`, the byte count of the selection (1, 2 or 4) and which
side was clicked. `App` polls `TakeRequest()` once after `Draw` and calls the existing component:

| Action | App does |
|---|---|
| Go to Memory | Scrub to that side's frame if it is attached, then `mHexEditor.GoTo(BusAddress(ref)); mPanels.hexEditor = true;`. A detached side has no frame to scrub to, so the item reads **Go to Memory (current view)** and says so: the Memory tab then shows the current frame, not the snapshot. |
| Add to Watch | `mWatchPanel.AddWatch(name, expr, type)`, type from the selection length. A watch tracks the running game, not frame A or B. |
| Break on Write | `mBreakpoints.AddMemory(BusAddress(ref), size, BpKind::MemWrite)` |
| View in Assembly | `mAssemblyPanel.GoTo(mAssemblyPanel.Cpu(), BusAddress(ref)); mPanels.assembly = true;` |
| Export Diff | `WriteCsv` into the existing export path. The menu item exports the clicked byte's region; the toolbar **Export...** button exports the region selected in the summary, or every region on All Memory (`Request::allRegions` / `exportRegion`). |

Watch and Break on Write act on the running emulator, so their tooltips say so; they do not depend
on which snapshot the byte was clicked in.

**Availability comes from `RegionTraits::caps`** (see the engine doc), not from address ranges.
The menu asks `MemoryComparePanel::ActionEnabled(action, region)`, which the tests pin:

| Region | Go to Memory | Watch | Break on Write | View in Assembly |
|---|---|---|---|---|
| LWRAM, HWRAM | yes | yes | yes | yes (SH-2) |
| VDP1 RAM, VDP2 RAM, VDP2 CRAM | yes | yes | yes (SH-2 and SCU-DMA writes) | no |
| VDP1/VDP2 Regs | yes | yes (register image) | yes (SH-2 writes to the register) | no |
| Sound RAM | yes | yes | yes, **for SH-2 and SCU-DMA writes only**: the SH-2 uploads sound programs and samples there, and the emulator's watchpoint matches every SH-2 effective address and SCU DMA write, but it cannot see the 68K sound CPU's own writes (`Integration/Mednafen/apply.py` notes). The enabled item says so in its tooltip | no: the 68K listing is in the Sound CPU tab, not `AssemblyPanel` (SH-2 cores only) |
| VDP1 FB | yes | yes (derived image) | **no**: an app-derived image written by VDP1 drawing, not a bus write | no |

A disabled Break on Write or View in Assembly carries a tooltip with the reason (Go to Memory and
Add to Watch are enabled in every region). There is no Find in ROM, since cartridge space
is not captured.

## Registration (pending: done in the hook-up pass)

1. `bool memoryCompare` in `struct Panels` (`App.h`).
2. A `PanelList()` row, group "Memory & Data".
3. `void DrawMemoryCompare()` in `App`, which wraps `mMemoryCompare.Draw` in
   `ImGui::Begin("Memory Compare")`.
4. `if (mPanels.memoryCompare) DrawMemoryCompare();` in `BuildUI`.
5. `DockBuilderDockWindow("Memory Compare", bAsm)` in `BuildDefaultLayout`.
6. An entry in `AdoptNewPanels` so existing `imgui.ini` layouts pick it up.

## Tests

`FrontEnd/tests/MemoryComparePanelTests.cpp`, using `ImGuiHarness` and hand-built
`MemSnapshot`s, so it needs neither the engine's capture path nor an emulator. Covered:

- address parsing (bus address, `Region+offset`, mirrors, refusals)
- the action capability table (VDP1 FB has no Break on Write; Sound RAM has it; View in Assembly only
  in work RAM)
- the empty state, the summary and grid tables, and the opening view
- clicking a summary row selects its region; clicking a byte selects it, on either side, and a
  right-click selects and opens the menu
- `GoTo`: a visible row keeps Changes Only, an elided one turns it off, and it follows the region
- a new pair of snapshots clears selection and region, and a `GoTo` still waiting when the pair
  changes is dropped
- identical snapshots show no grid

Still to cover when `App` is wired up, because they need the toolbar or popup items to be
reachable: each action raising exactly one request that is consumed once, detached sides disabling
Go to A / B, and the double-click focus trap on the summary rows.
