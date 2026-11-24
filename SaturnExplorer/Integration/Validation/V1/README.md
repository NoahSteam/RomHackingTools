# V1: deterministic guest program for the profiler

Purpose: a Saturn program whose profiler numbers are known in advance, so a run on the patched
emulator can be compared with `EXPECTED.md` (plan: `Docs/ProfilerFixPlan.md`, Phase 5, V1).

| File | What |
|---|---|
| `EXPECTED.md` | the expected observations and their derivations (written first, before the program) |
| `CHECKLIST.md` | fill-in table for a run |
| `build_v1.py`, `v1_program.py`, `sh2asm.py` | the guest (SH-2 assembly generated in Python), a tiny assembler, the disc builder |
| `run_v1.sh`, `run_v1.bat.txt` | build and run instructions |
| `tests/check_v1.py` (ctest `SaturnExplorerV1Guest`) | what can be checked without a BIOS |
| `tests/sh2sim.py`, `tests/v1_disasm.cpp` | test fixtures: a toy SH-2 interpreter; a helper that prints the repository decoder's view of a binary |

## Status: nothing here has been run on an emulator

No BIOS exists where this was written. Verified (by `tests/check_v1.py`): the emitted bytes decode, with
`FrontEnd/src/Debug/Sh2Disasm.cpp`, to the intended instructions; the loop cycle arithmetic (4N-2) in
`EXPECTED.md` follows from the instruction list; and the program, run on a fake machine, performs the
phase sequence, SMPC commands, VDP1 tables, DMA setup, sound-RAM programs and bus accesses `EXPECTED.md`
says it does. **Not** verified: that the BIOS boots the disc, that Mednafen's emulated peripherals react
as derived, and every profiler number.

## How it boots, and the limits

The guest is machine code emitted directly (no cross toolchain). To be loaded it needs a bootable
Saturn disc, and a bootable disc needs Sega's licensing block (bytes `0x100..0xDFF` of the first sector
area), which is Sega-copyrighted and must not be embedded or downloaded. So `build_v1.py --ip-bin PATH`
takes a file *you* own (an `IP.BIN` or any Saturn disc image), checks that its block is the one
Mednafen recognises (SHA-256 of `0x100..0xDFF`, the same test as `IsSaturnDisc()` in `ss.cpp`), copies that
block and the area/peripheral header bytes, replaces the product number and title (so Mednafen's game
database cannot attach another game's settings), puts the guest at IP offset `0xE00`, and writes a
32-sector image with an empty ISO 9660 volume and a `.cue`. Mednafen recognises it as a Saturn disc;
the BIOS runs the initial program.

Assumptions that could not be checked (labelled `A1`, `A2` in `build_v1.py`):

* **A1** the BIOS loads the initial program at `0x06002000`, so the code at offset `0xE00` runs at
  `0x06002E00`. The code at that address is position independent (a trampoline that copies the payload
  to `0x06010000` and jumps there), so only the entry address matters.
* **A2** the header words at `0xF0` (master start) and `0xF4` (slave start) are what the boot path uses.
  The script writes the master word, and points the slave word at a `sleep` loop inside the trampoline so
  that a slave started through it can never re-run the copy.

`build_v1.py --probe PATH` prints the header words of your file; a real master-start word in
`0x06000000..0x060FFFFF` supports A2. If the disc does not boot, adjust `--ip-load`, `--master-field`,
`--slave-field`. An alternative that was investigated and not implemented: Mednafen's bootable
cartridge ROM loader (`.ss`, `cart/bootrom.cpp`) maps a raw image at `0x02000000`, but how the BIOS
chooses to run it is not documented in the pinned source and could not be tested.

## Other limits

* After `SSHON` the slave runs whatever the BIOS gives it; the slave's busy/sleep split is not asserted.
* `-ss.dbg_cem full` (instruction cache emulation) numbers are expected to be wider; see `EXPECTED.md`.
* The function-attribution phase needs a `--enable-debugger` build and the Functions checkbox.
