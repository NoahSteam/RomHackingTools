# Memory Compare panel

`FrontEnd/src/MemoryComparePanel.{h,cpp}`. A dockable tab, its own class like `HexEditorPanel`,
held by `App` as `mMemoryCompare`. Part of [Frame Memory Compare](README.md).

## Layout

```
Memory Compare
+----------------------------------------------------------------------+
| Frame A (baseline)  1800   00:30.000   | Frame B (compare)  1884 ...  |
+----------------------------------------------------------------------+
| Region [All Memory v]  [x] Changes Only  [x] Context (2)               |
|                                         [Export...] [Go to A][Go to B]|
+--------------------------------+-------------------------------------+
| Region        Size  Changed  Ranges | HWRAM - 247 changed bytes (12)  |
| HWRAM        1 MiB    247   12     | Address  Frame A    Frame B  ASCII|
| LWRAM        1 MiB     86    8     | 06034F20 00 64 ...  00 5A ... ..  |
| ...                                |                                  |
+--------------------------------+-------------------------------------+
```

The left part (summary) and right part (hex diff) are split with `HorizontalSplitter` /
`SplitTopHeight` from `PanelWidgets.h`, not a hand-rolled splitter.

## Inputs

The panel owns no memory and runs no diff. `App` gives it:

- a `DiffResult` from `Diff()`, which holds the two `shared_ptr<const MemSnapshot>` (A and B)
- each marker's attached/detached state
- per-frame: `dt`

and rebuilds the visible row list (`BuildRows`) when the selected region, Changes Only or context
count changes. The panel never reads the emulator.

**There is no Refresh button.** The snapshots are immutable and the diff is deterministic, so
there is nothing to refresh: changing an option recomputes the rows from the same two snapshots
automatically. A control that could recapture memory would risk quietly comparing different frames
than the ones the user marked, so the panel has no way to recapture. Getting new data means setting
the markers again on the timeline. (The later live "Compare Against A" mode shows B as an explicit
`Live - frame N` card that visibly follows the timeline.)

## Header cards

Frame number, time (`frame / 60`, see README for the PAL caveat), and the snapshot's provenance:
`Live head` or `Rewind frame`, plus a **Detached** badge when the frame is no longer on the rewind
timeline (tooltip: "not in rewind history; snapshot kept"). There is deliberately no
"Snapshot: Valid/Invalid" line. A snapshot only exists if its capture fully succeeded, so showing
one is already the statement that it is complete; a failed capture is reported at the timeline when
the user tries to mark, and no card is created.

"Go to A / Go to B" scrub the timeline to that frame. They are disabled for a detached side, with
the tooltip saying why.

## Region summary table

One row per captured region plus an "All Memory" entry, columns Region, Size, Changed Bytes,
Changed Ranges. Counts are exact (`RegionDiff::changedBytes` / `rangeCount`); only the stored range
list is capped, and the cap does not affect the hex view or export, which rescan the snapshots. Rows use `RowSelectable`; selecting one filters the
hex diff. Regions with zero changes are shown dimmed and are not selectable targets for "next
change". The colour swatch beside a region follows its type (work RAM, VDP1, VDP2, colour RAM,
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
  `RegisterImage` regions the row label is region-qualified (`VDP1 FB+0x01230`, `VDP2 Regs+0x0E0`),
  because those are not bus-addressed memory; the rest of the app keeps using the Memory tab's
  addresses for them. Selection and every action carry a `RegionRef`, not a raw address.
- **Jump to Address** box: accepts a bus address (mirrors folded) or `Region+offset`, selects that
  region, and scrolls to the row. If Changes Only elides it, the panel switches Changes Only off for
  that jump and says so.
- Byte selection is keyed by absolute address (as in `HexEditorPanel`), so it survives a region
  switch or a row-list rebuild.
- Read-only. There is no editing here; the Memory tab is for that.

## Context menu actions

Right-click a byte. The panel does not perform the action. Like `HexEditorPanel`, it raises a
request carrying a `RegionRef`, the byte count of the selection (1, 2 or 4) and which side was
clicked, and `App` polls it after `Draw` and calls the existing component:

| Action | Request | App does |
|---|---|---|
| Go to Memory | `TakeGoToMemoryRequest` | Scrub to that side's frame if it is attached, then `mHexEditor.GoTo(BusAddress(ref)); mPanels.hexEditor = true;`. A detached side has no frame to scrub to, so the item reads **Go to Memory (current view)** and says so: the Memory tab then shows the current frame, not the snapshot. |
| Add to Watch | `TakeWatchRequest` | `mWatchPanel.AddWatch(name, expr, type)`, type from the selection length. A watch tracks the running game, not frame A or B. |
| Break on Write | `TakeBreakpointRequest` | `mBreakpoints.AddMemory(BusAddress(ref), size, BpKind::MemWrite)` |
| View in Assembly | `TakeAssemblyRequest` | `mAssemblyPanel.GoTo(mAssemblyPanel.Cpu(), BusAddress(ref)); mPanels.assembly = true;` |
| Export Diff | `TakeExportRequest` | `WriteCsv` into the existing export path |

Watch and Break on Write act on the running emulator, so the tooltip says so; they do not depend on
which snapshot the byte was clicked in.

**Availability comes from `RegionTraits::caps`** (see the engine doc), not from address ranges:

| Region | Go to Memory | Watch | Break on Write | View in Assembly |
|---|---|---|---|---|
| LWRAM, HWRAM | yes | yes | yes | yes (SH-2) |
| VDP1 RAM, VDP2 RAM, VDP2 CRAM | yes | yes | yes (SH-2 and SCU-DMA writes) | no |
| VDP1/VDP2 Regs | yes | yes (register image) | yes (SH-2 writes to the register) | no |
| Sound RAM | yes | yes | **no**: its writer is the 68K, and the emulator's watchpoints see SH-2 instructions and SCU DMA only (`Integration/Mednafen/apply.py` notes) | no: the 68K listing is in the Sound CPU tab, not `AssemblyPanel` (SH-2 cores only) |
| VDP1 FB | yes | yes (derived image) | **no**: an app-derived image written by VDP1 drawing, not a bus write | no |

A disabled item carries a tooltip with the reason. There is no Find in ROM, since cartridge space
is not captured.

## Registration (done in the hook-up pass)

1. `bool memoryCompare` in `struct Panels` (`App.h`).
2. A `PanelList()` row, group "Memory & Data".
3. `void DrawMemoryCompare()` in `App`, which wraps `mMemoryCompare.Draw` in
   `ImGui::Begin("Memory Compare")`.
4. `if (mPanels.memoryCompare) DrawMemoryCompare();` in `BuildUI`.
5. `DockBuilderDockWindow("Memory Compare", bAsm)` in `BuildDefaultLayout`.
6. An entry in `AdoptNewPanels` so existing `imgui.ini` layouts pick it up.

## Tests

`FrontEnd/tests/MemoryComparePanelTests.cpp`, using `ImGuiHarness` and hand-built
`MemSnapshot`s, so it needs neither the engine's capture path nor an emulator:

- the summary row and a hex row are clickable: a click selects, a double click on a row does not
  steal focus (the `RowSelectable` traps described in `CLAUDE.md`)
- selecting a region changes the visible rows; Changes Only and context count rebuild them
- jump to an elided address switches Changes Only off
- each context-menu action raises exactly the right request with the right address, and a request is
  consumed once
- detached sides disable Go to A / B, and make Go to Memory read "(current view)"
- each region's menu items match the capability table (Sound RAM and VDP1 FB have no Break on Write)
- there is no control that can change which snapshots are shown; changing Changes Only or context
  rebuilds rows from the same snapshots (pointer identity of `DiffResult::a`/`b` is unchanged)
- a truncated region (more than `kMaxStoredRanges` changes) still shows every changed row
- a `Metrics()` accessor (as on `HexEditorPanel`) exposes row geometry so layout assertions do not
  depend on pixels
