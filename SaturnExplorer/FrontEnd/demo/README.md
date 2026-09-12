# Saturn Explorer — Demo Mode & feature-tour scripts

Demo Mode plays a **`.sedemo`** script that drives the real UI — showing, soloing, and
focusing panels, selecting a VDP1 command, toggling render layers — so a feature walkthrough
can be screen-recorded (OBS) with almost no manual clicking. **Narration is read separately**
from the generated `Narration_*.md`; the tour draws nothing on screen except an optional
operator HUD you turn off for a clean take.

## Using it

1. Build/run Saturn Explorer. **Load a savestate first** (Session → Source → Load Dump) so the
   app is sitting on an interesting scene — the scripts assume data is already loaded.
2. Toolbar **Demo** menu → **Load demo script…** → pick a `.sedemo` from this folder.
3. **F7** starts/stops. Default is **manual** mode: press **F8** to advance to the next beat
   (**Shift+F8** to go back). This keeps picture and voice in sync while you read the
   narration live. Turn on **Auto-advance** in the Demo menu for hands-free timed playback
   (each beat dwells for its `hold` seconds).
4. The **Operator overlay** (Demo menu) shows `beat i/N · id · controls`. Leave
   **Overlay shows narration** OFF for recording; turn it on only if you want a teleprompter
   and will crop it out in OBS.

## Scripts here

| Script | Source needed | Length | Covers |
|---|---|---|---|
| `Highlights.sedemo` | a savestate | ~2–3 min | the graphics story: render → click a sprite → 3D explode → peel layers → trace to texture/VRAM → references |
| `LiveDebugging.sedemo` | a live emulator | ~4–6 min | SH-2 Assembly, breakpoints + call stack, **tracepoints** (the game narrating itself), Watch |

Record each as its own clip and edit them together — the two need different sources, so one
continuous take isn't required.

## Script format

Blank lines are ignored; a line whose first non-space character is `#` is a comment.

```
@beat <id> [hold <seconds>]      # start a beat (hold is the auto-mode dwell; default 6)
  note "spoken narration line"   # read aloud — never drawn on screen
  solo vdpOutput worldView       # show exactly these panels, hide the rest
  show textureViewer             # reveal one panel
  hide log                       # hide one panel
  focus selectedObject           # bring a panel's tab forward
  select 42                      # select VDP1 command #42 (needs a loaded source)
  layer vdp1 off                 # toggle a render layer on/off
  load "path/to/state.yss"       # load a savestate/dump (optional; usually pre-loaded)
  command step                   # a safe toolbar command: pause / step / screenshot
```

### Panel keys (the `PanelList()` keys)
`vramMap, archiveExplorer, searchRom, vdpOutput, worldView, commandList, vdp1Table, vdp2Table,
registers, colorRam, textureViewer, paletteViewer, references, selectedObject, watch, assembly,
hexEditor, controller, log, actions` (Tracepoints), `callStack, breakpoints, ramSearch, accessLog,
soundCpu, sound, discExplorer`.

### Layer names (for `layer <name> on|off`)
`vdp1` (sprites), `wireframe`, `bounds`, `objnums`, `nbg0`, `nbg1`, `nbg2`, `nbg3`, `rbg0`,
`window`, `colorcalc`, `shadow`.

### Notes
- `select N` and the exact panel mix depend on your loaded savestate — tweak the indices to
  suit the scene you recorded on.
- Unknown verbs and layer names are ignored (a newer script still loads on an older build).
- `command` is limited to `pause`/`step`/`screenshot` — Demo Mode never launches a process or
  writes a disc, so an unattended auto-play can't do anything surprising.
