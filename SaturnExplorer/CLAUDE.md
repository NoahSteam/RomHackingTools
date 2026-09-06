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
trying to type in a cell. **Any row with an interactive cell must take its flags from
`RowSelectableFlags` in `FrontEnd/src/PanelWidgets.h`**, which the tests exercise directly.

Note the limit of that coverage: the tests pin the flag decision and the resulting hit-test
behaviour, not the panels' own layout. A new panel that hand-rolls its Selectable flags is
not protected — route it through the helper.

## Conventions

- Commit directly to `master` and push. No branches or PRs.
- C++14 in Core and the savestate driver (they build for the web too).
- Comments explain *why* — the hardware quirk, the reason a guard exists. The code already
  says what it does.
