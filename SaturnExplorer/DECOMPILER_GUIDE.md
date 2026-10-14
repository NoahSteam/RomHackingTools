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
| The panel | Turn on **Follow PC**: it shows the function the chosen CPU is in. Or type an entry address (hex, any mirror, e.g. `06004000`) and press Enter. A mirror is decompiled as code running there: `26004000` runs the same bytes through the cache-through area, so a `mova` there yields a `0x26…` pointer. The function keeps its canonical name. |
| SH-2 Assembly | Right-click a line → **Decompile Here**. |
| Call Stack | Right-click a frame → **Decompile**. |
| Registers | In the **SH-2** tab, right-click the **PC value** (not its label) → **Decompile**. |

The combo picks **Master SH-2** or **Slave SH-2**. That only decides whose PC Follow PC reads. With Follow PC on, switching follows the other CPU. With it off, the output stays, because both CPUs share RAM.

## Reading the output

- The status line shows the source and frame, and the function's name and entry. It also shows the panel's state: decompiling, stale, unsupported, or out of time.
- **Click a line** to jump SH-2 Assembly to the first instruction it came from.
- **Right-click a line** for these:
  - **Show in SH-2 Assembly**.
  - **Rename Function...** and **Rename Callee 0x...**: names go into the same store Call Stack uses. Call Stack shows a name only on frames whose function it has identified, not on "Func unknown" frames. SH-2 Assembly has no label column, so a name does not show on its rows.
  - **Set Function Start Here**: use it when the finder guessed the entry wrong.
  - **View in Memory**.
  - **Copy Line** and **Copy Function**.
- **Hardware registers** have names, such as `VDP2_TVMD`, `SCU_D0R` and `SH2_DVDNT`. There are 335: VDP1, VDP2, SCU, SMPC, SCSP, the CD block, `MINIT`/`SINIT` and the SH-2's own peripherals. Bus registers are named at their cached and cache-through addresses. Every access to a register area stays in the output in the order the code makes it: a write before a poll, the poll itself, a write the next one replaces, and a read whose value is unused. A read whose value is unused is also kept when its address is computed and never becomes a constant, since it may be a register. A read of ordinary memory whose address does become a constant is dropped as usual.
- **Hardware explanations** appear as a trailing `// …` comment on the line. They say what the name alone does not:
  - a documented register pair accessed whole, such as a 32-bit VDP2 scroll write;
  - which watchdog register a keyed write selects;
  - a register reached through a mirror, which the output shows by address;
  - what a register returns when its reads and writes reach different fields, such as `SCU_IST` or `CD_CR1`, if the function also writes it.
- **Volatile memory.** Sound RAM, shared with the 68000, is always treated like a register. For other memory another processor changes, such as a mailbox shared with the slave SH-2, click **Volatile** in the toolbar and list the ranges, for example `0x26001000-0x26001003, 0x06002000`. Every access there is then kept, through every address that reaches it: both partitions, the bus repeats and high work RAM's 1 MiB mirror. Each range is widened to whole longwords, so a word access touching one marked byte is kept too. A literal-pool word in a marked range is read, not folded from the snapshot, and a jump table in one is not recovered into a fixed `switch`: the jump stays indirect, with a warning. Hardware areas are treated the same way. Changing the list marks the output stale, and signatures learned under the old list are no longer lent. The list is saved in `settings.ini` under `[decompiler] volatile`. Without it, a poll of shared work RAM can print as `do {} while (true);`.
- **Calls into a function already decompiled** this session use its recovered signature: argument count, types and return. A signature is used only when its recovery was complete. That means no bad instruction or unresolved jump, no jump table recovered only in part, an analysis that settled, every function it calls already has a known signature (and it does not call itself), every argument it reads is in the signature, and every return goes back to the caller. So decompile callees first: a caller shown before its callees is decompiled again, with their signatures, the next time it is shown. A function that calls into the BIOS without an image, or through a computed pointer, never lends its signature.
- **Comments** you add in SH-2 Assembly (right-click → **Edit Comment**) appear as a trailing `// note` on the first line decompiled from that instruction. Editing one never re-runs the decompiler.
- **At a halt on `jsr @rN`, `jmp @rN`, `braf` or `bsrf`**, the call's line and the status line show where the call goes this time, for example `r2 = 0x06031510 at the halt`. The target is computed, so another call may go elsewhere.

## The BIOS

Calls into the BIOS stop at the BIOS boundary unless you give the panel an image. Click the **BIOS** button in the panel's toolbar → **Choose BIOS Image...** and pick your own dump; it is never shipped, and it must be 512 KiB or smaller. The image is mapped at `0x00000000`, so BIOS functions decompile and are named `bios_0x…` until you rename them. The path is saved in `settings.ini` under `[decompiler]`.

## When the output is stale or missing

The output is always for a specific snapshot. If what you are looking at changes, the output is marked rather than silently replaced. Examples of a change: a Hex Editor write, a scrub, the emulator running on, a rename, or a new BIOS image.

| You see | Meaning | What to do |
|---|---|---|
| **stale: frame N** | The emulator has moved on since this was decompiled. | **Refresh**, or turn on **Auto-refresh**. Auto-refresh only works with Follow PC on, refreshes at most every 30 frames, and is off by default. |
| **PC-relative load in a delay slot at 0x… : not trustworthy** (amber, greyed) | A `mov.l @(disp,PC)` or `mova` sits in the delay slot of a branch whose slot PC the engine cannot model. `bra` and `bsr` are handled; `bt/s`, `bf/s`, `jmp`, `jsr`, `rts` and `braf` are not. | Read the assembly for that slot. The rest of the output is shown for orientation only. |
| **Flows into … at 0x… ; the function is cut off there** (amber; "and at N more places" when there are several) | Flow reached memory that is not captured: the BIOS with no image, or an unmapped area. That path stops there. | Give the panel a BIOS image. If the code should not go there, the entry is probably wrong: **Set Function Start Here**. |
| **Flow runs into bytes that do not decode at 0x…** (amber) | Flow reached data in captured memory: a literal pool, a jump table, or code read from the wrong place. That path stops there. Four or more such places are flagged as "the entry is probably not code". | Check the entry in SH-2 Assembly; **Set Function Start Here** if it is wrong. |
| **0x…: Treating indirect jump as call** (or **as return**, **Could not recover jumptable**) (amber) | The engine could not resolve a `braf`/`jmp @rN` switch. Its cases are missing from the output, which shows one indirect call instead. | Read the cases in SH-2 Assembly. |
| **0x…: Sanity check requires truncation of jumptable: cases may be missing** (or **Second-stage recovery error: …**) (amber) | The engine recovered a switch only in part. The cases it dropped are missing from the output, and the function lends no signature. A note about case labels ("may not be properly labeled", "Bad switch case") means every case is there but its label may be wrong. | Read the table in SH-2 Assembly. |
| **0x…: jmp is taken for a tail call to 0x…** (amber) | The engine read an indirect jump with one possible target as a tail call, and assumes a return after it. When the target is not captured, the panel says so: what runs there is not shown. | Check the target; give the panel a BIOS image if it is there. |
| **0x…: flow runs into NAME at 0x…, another function's entry** (amber) | A tail branch, a fall-through or shared code reaches a known function's entry. That function's code is shown as part of this one. | Expected for a tail call. Otherwise check both entries. |
| **0x… is not reached from this entry** (amber) | The function found for an address (the PC, or the one you asked about) never gets there. The finder picked the wrong entry. | **Set Function Start Here** on the real entry. |
| **Entry: …** (amber) | The finder is unsure of the entry, and says why: an `rts` that may end the function, a branch before that `rts` that jumps past it (an early return), a jump table of unknown length, or code that never reaches the address. The confidence shown beside the source drops by 20. An entry found only by the `rts` scan always shows "guessed from the previous rts". | Check the entry; correct it if needed. |
| **0x…: reads REG, which is write-only** / **writes REG, which is read-only** (amber) | The code reads back a register the hardware does not return, or writes one that ignores writes. The output shows the access as written. | Expected in some code. The value read is not the value written. |
| **0x…: one 4-byte write covers REG1 and REG2** (amber) | One access spans two registers that the manuals do not document as a pair. The output names only the first, with a `_` prefix. Documented pairs, such as the VDP2 scroll values, are explained on the line instead. | Check what the code meant to write. |
| **0x…: watchdog write with key …: ignored** (amber) | A word write to the watchdog's ports at `0xFFFFFE80`/`0xFFFFFE82` with a key the hardware rejects. | Check the value. |
| **N more warnings** | More than four of the amber lines above. | Hover for the rest. |
| **too large to decompile within 10 s** | The time budget ran out. | **Refresh** retries once on demand. It is not retried automatically. |
| **Decompiler disabled for this session** | A cancelled run did not stop in time, or the engine failed badly. | Restart Saturn Explorer. The rest of the app is unaffected. |
| **Decompiler unavailable: <path>** | The language files could not be written to the config directory. | Fix permissions on that directory and restart. |

## Limits

- The output is pseudocode for reading, not source that compiles.
- Function boundaries are found heuristically, from a known entry or backwards to the previous `rts` plus slot. The scan skips literal pools and `mova` jump tables, and steps past a function that ends in a tail jump. A known entry keeps an address after an `rts` when flow from the entry reaches it (an early return). What the finder cannot settle is shown as an amber line. Expect to correct some entries with **Set Function Start Here**.
- Indirect calls stay indirect unless you are halted on them (see above).
- Only one level of pointer type is recovered for signatures. Structures are not inferred.

## Licence

The engine is Ghidra's decompiler (Apache-2.0, NSA), vendored with the small patches listed in `FrontEnd/third_party/ghidra-decompiler/VENDOR.md`. **Help → About → Third-party** lists every third-party component. Their licence texts ship beside the app.
