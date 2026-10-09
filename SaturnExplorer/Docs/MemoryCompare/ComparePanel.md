# Memory Compare panel

`FrontEnd/src/MemoryComparePanel.{h,cpp}`. A dockable tab, its own class like `HexEditorPanel`,
held by `App` as `mMemoryCompare`. Part of [Frame Memory Compare](README.md).

## Layout

```
Memory Compare
+----------------------------------------------------------------------+
| Frame A (baseline)  1800   00:30.000   | Frame B (compare)  1884 ...  |
+----------------------------------------------------------------------+
| Region [All Memory v]  [x] Changes Only  [x] Context (2)  [Refresh]   |
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

- two `shared_ptr<const MemSnapshot>` (A and B)
- a `DiffResult` from `Diff()`
- per-frame: `dt`

and rebuilds the visible row list (`BuildRows`) when the selected region, Changes Only or context
count changes. The panel never reads the emulator.

## Header cards

Frame number, time (`frame / 60`, see README for the PAL caveat), and a `Snapshot: Valid` or
`Snapshot: Invalid` line. "Go to A / Go to B" scrub the timeline to that frame when it is still in
the ring, and are disabled with a tooltip when it has been evicted.

## Region summary table

One row per captured region plus an "All Memory" entry, columns Region, Size, Changed Bytes,
Changed Ranges (shown as `N+` when truncated). Rows use `RowSelectable`; selecting one filters the
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
- **Jump to Address** box: parses hex, selects the region containing it, and scrolls to the row
  (the row must exist; if Changes Only elides it, the panel switches Changes Only off for that jump
  and says so).
- Byte selection is keyed by absolute address (as in `HexEditorPanel`), so it survives a region
  switch or a row-list rebuild.
- Read-only. There is no editing here; the Memory tab is for that.

## Context menu actions

Right-click a byte. The panel does not perform the action. Like `HexEditorPanel`, it raises a
request that `App` polls after `Draw`, and `App` calls the existing component:

| Action | Request | App does |
|---|---|---|
| Go to Hex Editor | `TakeGoToMemoryRequest` | `mHexEditor.GoTo(addr); mPanels.hexEditor = true;` |
| Add to Watch | `TakeWatchRequest` | `mWatchPanel.AddWatch(name, expr, type)`; type from the selection length (1, 2 or 4 bytes) |
| Break on Write | `TakeBreakpointRequest` | `mBreakpoints.AddMemory(addr, size, BpKind::MemWrite)` |
| View in Assembly | `TakeAssemblyRequest` | `mAssemblyPanel.GoTo(cpu, addr); mPanels.assembly = true;` |
| Export Diff | `TakeExportRequest` | `ToCsv(...)` then the existing export path |

Availability follows the region: View in Assembly is offered for LWRAM and HWRAM only, because
`AssemblyPanel::GoTo(cpu, addr)` addresses the two SH-2 cores (0 master, 1 slave) and the 68K
listing lives in the Sound CPU tab. It targets `mAssemblyPanel.Cpu()`, the core the panel is
already viewing. It is greyed out elsewhere. Break on Write is offered for the regions the live
driver's breakpoint set can express; confirm the sound RAM and VDP cases against `BreakpointManager`
when wiring and grey out what the driver cannot deliver, rather than adding a breakpoint that will
never fire. There is no Find in ROM, since cartridge space is not captured.

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
- evicted frames disable Go to A / B
- a `Metrics()` accessor (as on `HexEditorPanel`) exposes row geometry so layout assertions do not
  depend on pixels
