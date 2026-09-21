# Saturn Explorer — notes for agents

A Sega Saturn ROM-hacking and debugging tool: a portable analysis core, a Dear ImGui
front end, and a patched Mednafen that talks to it over a local socket.

## Where to read first

| Topic | Doc |
|---|---|
| Layout, the two seams, what lives where | `ARCHITECTURE.md` |
| Building (CMake, three ImGui backends, web) | `BUILD.md` |
| VDP1 / VDP2 rendering semantics | `VDP1_GUIDE.md`, `VDP2_GUIDE.md` |
| Log panel, tracepoints, execution actions | `EXECUTION_ACTIONS.md` |

`_emu/mednafen` is a working checkout of Mednafen that `Integration/Mednafen/apply.py`
patches. It is untracked on purpose — do not commit it. It is also the best available
reference for Saturn hardware behaviour when a guide is silent: prefer reading
`_emu/mednafen/src/ss/*.cpp` over guessing at a register's semantics.

> **Before bumping the Mednafen fork or editing the patch, read
> [Integration/Mednafen/README.md](Integration/Mednafen/README.md) → "Rewind".** The savestate
> hooks the rewind and save-state features are built on call Mednafen internals that are not a
> stable interface, and `apply.py` falls back to a compiling stub when they don't match. Both the
> fallback and a subtly-wrong binding fail *silently* — no error, no log line, the feature just
> does nothing. That section says how to verify the real path is live.

## Build and test

```
cmake -S . -B build && cmake --build build -j8
ctest --test-dir build --output-on-failure
```

## Debugging rendering

`build/bin/se-render <dump.sedump> --out frame.ppm` composites a saved memory dump
headlessly, with `--reference`/`--diff`/`--tolerance` for golden-frame comparison. A
`.sedump` reproduces a rendering bug deterministically without running the emulator, so
reach for it before trying to reproduce anything by playing the game.

## Testing ImGui panels

The panels are not separable from `App`, but ImGui's *interaction* layer — layout, hit
testing, hover, activation, keyboard routing — needs no renderer or window. `FrontEnd/tests/
ImGuiHarness.h` drives ImGui headlessly so those behaviours can be asserted on;
`PanelInteractionTests.cpp` uses it.

This matters because a whole class of panel bug is invisible to ordinary unit tests and to
screenshots alike: the widget renders, it even reports as hovered, but clicks never reach
it. The known trap is a table row whose `Selectable` uses `SpanAllColumns`. It is submitted
before the cells to its right and ImGui's hit test rejects an item once an earlier one owns
`HoveredId`, so without `AllowOverlap` the row swallows every click in it — and if the row
navigates on double-click, it will run off and steal keyboard focus while the user is
trying to type in a cell.

A second trap rides along with the first. A row holding edit boxes is a full frame tall, so
its `Selectable` needs that explicit height or its highlight covers one line instead of the
row — but pairing that height with `AlignTextToFramePadding` offsets the Selectable's box
(it is derived from `CursorPos + CurrLineTextBaseOffset`), the row grows to fit, and every
cell in it then sits above centre.

**A whole-row Selectable must go through `RowSelectable` in
`FrontEnd/src/PanelWidgets.h`**, which settles all three things together: the overlap flag,
the frame height, and the label's vertical centring. `RowSelectableFlags` remains for the
rows that need to OR in extra flags. Text cells *beside* the row Selectable still want
`AlignTextToFramePadding`; it is only the Selectable itself that must not have it.

Note the limit of that coverage: the tests pin the flag decision, the hit-test behaviour and
the row's vertical centring, not the panels' own layout. A new panel that hand-rolls its
Selectable is not protected — route it through the helper.

## Conventions

- Commit directly to `master` and push. No branches or PRs.
- C++14 in Core and the savestate driver (they build for the web too).
- Comments explain *why* — the hardware quirk, the reason a guard exists. The code already
  says what it does.
