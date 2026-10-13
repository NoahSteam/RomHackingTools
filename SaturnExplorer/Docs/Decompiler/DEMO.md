# SH-2 Pseudocode — demo script

A walkthrough of about 10 minutes that shows each decompiler feature once, in an order where every step
sets up the next. It works with any commercial game. Nothing game-specific is in this repo, so
bring your own:
- a Mednafen savestate (`.mc0`) or a Yabause `.yss` of a game in play;
- a patched Mednafen for the live part (`Integration/Mednafen/`);
- optionally, your own BIOS dump.

Before you start:
- Reset the layout with **Windows → Reset Layout**, so SH-2 Pseudocode sits beside SH-2 Assembly.
- Keep **Auto-refresh off**; step 6 turns it on.

| # | Do | Say / point at |
|---|---|---|
| 1 | Open the savestate. In SH-2 Pseudocode, turn on **Follow PC**. | The function the master CPU is in, as C-like pseudocode, in-process, with no Ghidra install. The status line shows the source, frame, name and entry. |
| 2 | Click a line with a call or an assignment. | SH-2 Assembly jumps to the instruction it came from. Lines map back to addresses. |
| 3 | Find a line that writes `VDP2_…`, `SCU_…` or `SMPC_…` (most frame loops touch one). | Hardware registers are named, not raw `*(ushort *)0x25f80000`. |
| 4 | Right-click a callee → **Rename Callee 0x…**, and give it a name. | The name appears at once here, in SH-2 Assembly and in Call Stack. Then **Decompile Here** on that callee in SH-2 Assembly, and come back: the caller's call now carries the callee's recovered signature. |
| 5 | In SH-2 Assembly, right-click an instruction the pseudocode shows → **Edit Comment**. | It appears as `// note` on that line, and nothing is re-decompiled. |
| 6 | Go live: connect to Mednafen running the same game. Leave it running. | Follow PC shows the output with **stale: frame N** as the game moves on. It is never silently swapped. Press **Refresh**, then turn on **Auto-refresh**: it follows the PC at most every 30 frames. |
| 7 | Pause, then step until the PC sits on a `jsr @rN` (Call Stack and SH-2 Assembly help). | The call's line says `rN = 0x… at the halt`, naming the target if it is named. It applies to this call, this time. |
| 8 | Switch the combo to **Slave SH-2**. | The output stays, because RAM is shared; Follow PC now reads the slave's PC. |
| 9 | Optional: **BIOS → Choose BIOS Image…**, then follow a call into `0x00000…`. | BIOS functions decompile and are named `bios_0x…`. The image is the user's own and is never shipped. |
| 10 | **Help → About → Third-party**. | Ghidra's engine is Apache-2.0, modified by the listed patches; its licence texts ship with the app. |

Optional: to show the A3 "not trustworthy" banner, write fixture 3's `bf/s` case into a copy of a savestate. The words come from `Docs/Decompiler/spike/tools/fixture3.py`, and the procedure is in `Docs/FunctionalityVerification/13_DECOMPILER_STEP3.md`, row 11. Do not commit that savestate.
