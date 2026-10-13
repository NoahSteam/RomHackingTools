# SH-2 Pseudocode (decompiler) guide

The **SH-2 Pseudocode** panel turns the SH-2 function you are looking at into C-like pseudocode.
It runs Ghidra's decompiler engine in-process, against a snapshot of the machine you are looking
at: a savestate, a paused or running emulator, or a recorded frame. Nothing is sent anywhere,
and no Java or Ghidra install is needed.

Design and rationale: `Docs/Decompiler/DESIGN.md`. Build plan and what was built: `Docs/Decompiler/PLAN.md`.

## Opening it

**Windows → Debugger → SH-2 Pseudocode**. By default it docks beside SH-2 Assembly. To pick a function, use any of these:

| From | How |
|---|---|
| The panel | Turn on **Follow PC**: it shows the function the chosen CPU is in. Or type an entry address (hex, any mirror, e.g. `06004000`) and press Enter. |
| SH-2 Assembly | Right-click a line → **Decompile Here**. |
| Call Stack | Right-click a frame → **Decompile**. |
| Registers | Right-click **PC** → **Decompile**. |

The combo picks **Master SH-2** or **Slave SH-2**. That only decides whose PC Follow PC reads: both CPUs share RAM, so the output stays when you switch.

## Reading the output

- The status line shows the source and frame, and the function's name and entry. It also shows the panel's state: decompiling, stale, unsupported, or out of time.
- **Click a line** to jump SH-2 Assembly to the first instruction it came from.
- **Right-click a line** for these:
  - **Show in SH-2 Assembly**.
  - **Rename Function…** and **Rename Callee 0x…**: names are shared with SH-2 Assembly and Call Stack.
  - **Set Function Start Here**: use it when the finder guessed the entry wrong.
  - **View in Memory**.
  - **Copy Line** and **Copy Function**.
- **Hardware registers** have names, such as `VDP2_TVMD` and `SCU_D0R`. There are 256 VDP1, VDP2, SCU, SMPC and SCSP registers, at both their cached and cache-through addresses.
- **Calls into a function already decompiled** this session use its recovered signature: argument count, types and return.
- **Comments** you add in SH-2 Assembly (right-click → **Edit Comment**) appear as a trailing `// note` on the first line decompiled from that instruction. Editing one never re-runs the decompiler.
- **At a halt on `jsr @rN`, `jmp @rN`, `braf` or `bsrf`**, the call's line and the status line show where the call goes this time, for example `r2 = 0x06031510 at the halt`. The target is computed, so another call may go elsewhere.

## The BIOS

Calls into the BIOS stop at the BIOS boundary unless you give the panel an image. Use **BIOS → Choose BIOS Image…** and pick your own dump; it is never shipped, and it must be 512 KiB or smaller. The image is mapped at `0x00000000`, so BIOS functions decompile and are named `bios_0x…` until you rename them. The path is saved in `settings.ini` under `[decompiler]`.

## When the output is stale or missing

The output is always for a specific snapshot. If what you are looking at changes, the output is marked rather than silently replaced. Examples of a change: a Hex Editor write, a scrub, the emulator running on, a rename, or a new BIOS image.

| You see | Meaning | What to do |
|---|---|---|
| **stale: frame N** | The emulator has moved on since this was decompiled. | **Refresh**, or turn on **Auto-refresh**. Auto-refresh only works with Follow PC on, refreshes at most every 30 frames, and is off by default. |
| **PC-relative load in a delay slot at 0x… : not trustworthy** (amber, greyed) | A `mov.l @(disp,PC)` or `mova` sits in the delay slot of a branch whose slot PC the engine cannot model. `bra` and `bsr` are handled; `bt/s`, `bf/s`, `jmp`, `jsr`, `rts` and `braf` are not. | Read the assembly for that slot. The rest of the output is shown for orientation only. |
| **Flows into … ; the function is cut off there** | The function falls through into the next one. | Usually the finder's guess; **Set Function Start Here** on the real entry. |
| **too large to decompile within 10 s** | The time budget ran out. | **Refresh** retries once on demand. It is not retried automatically. |
| **Decompiler disabled for this session** | A cancelled run did not stop in time, or the engine failed badly. | Restart Saturn Explorer. The rest of the app is unaffected. |
| **Decompiler unavailable: <path>** | The language files could not be written to the config directory. | Fix permissions on that directory and restart. |

## Limits

- The output is pseudocode for reading, not source that compiles.
- Function boundaries are found heuristically: backwards to the previous `rts` plus slot, or from a known entry. Expect to correct some with **Set Function Start Here**.
- Indirect calls stay indirect unless you are halted on them (see above).
- Only one level of pointer type is recovered for signatures. Structures are not inferred.

## Licence

The engine is Ghidra's decompiler (Apache-2.0, NSA), vendored with the small patches listed in `FrontEnd/third_party/ghidra-decompiler/VENDOR.md`. **Help → About → Third-party** lists every third-party component. Their licence texts ship beside the app.
