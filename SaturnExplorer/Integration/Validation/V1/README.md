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

## Status: run once on a real machine; numbers confirmed

The expected numbers were written first (before the program existed) and are now confirmed on a real run 2026-11-25 (Windows, Mednafen 1.32.1 patched, real BIOS, both cache modes, 48/48 checklist rows matched; exact matches for the phase 0 baseline, the 15 phase-3 bus buckets, VDP1 commands/work, SCU DMA byte counts, slave on-time and the 68000 phases; in-range: phase 1 busy 160,073, func_a/func_b 12,003/4,003, stall ratio 3.98, 68000/SH-2 clock ratio 0.42009).
Not checked on that run: the readings came through a headless recorder on the live connection, not the Profiler panel; hook-disable checks were done for slave on-time, VDP1 work and the bus buckets only (the F5, F9 and F11 hook checks were not run). The disc needed a manual fix to boot (an empty root directory made the BIOS drop to its CD player); `build_v1.py` now writes a real first file. The guest was changed after that run (see README: display bring-up for the backdrop colour); in the fake-machine simulation every steady frame performs exactly the same accesses at the same addresses as before (the change is one-time start-up code placed after all other payload bytes); not re-run on the emulator.

Findings from that run, and what changed because of them: (1) the disc needed one real file in the ISO root
directory or the BIOS drops to its CD player (`build_v1.py` now writes `V1.BIN;1`, the payload's first bytes,
and `check_v1.py` parses the PVD and root directory); (2) the backdrop colour stayed black, because the display
was off (fixed by a one-time display bring-up that adds no access to any steady frame; the fix itself has not
been seen on the emulator); (3) `-ss.dbg_cem full` gave the same steady-state figures as the default, which
`EXPECTED.md` ("Cache modes") now explains.

Before the real run, only the following was verified (by `tests/check_v1.py`, which still runs): Verified (by `tests/check_v1.py`): the emitted bytes decode, with
`FrontEnd/src/Debug/Sh2Disasm.cpp`, to the intended instructions; the loop cycle arithmetic (4N-2) in
`EXPECTED.md` follows from the instruction list; and the program, run on a fake machine, performs the
phase sequence, SMPC commands, VDP1 tables, DMA setup, sound-RAM programs and bus accesses `EXPECTED.md`
says it does. **Not** verified by that script alone: the BIOS boot path, the emulated peripherals and the profiler numbers (the real run covered them as stated above).

## How it boots, and the limits

The guest is machine code emitted directly (no cross toolchain). A bootable disc needs Sega's licensing
block (bytes `0x100..0xDFF` of the first sector area), which is Sega-copyrighted and must not be embedded
or downloaded. So `build_v1.py --ip-bin PATH` takes a file *you* own (an `IP.BIN` or any Saturn disc image)
and checks that its block is the one Mednafen recognises (SHA-256 of `0x100..0xDFF`, the test in
`IsSaturnDisc()`, `ss.cpp`).

Layout, from the documented Saturn system ID format (run against a real BIOS on 2026-11-25, with the root-directory file added):

| IP offset | What | What the builder does |
|---|---|---|
| `0x000..0x0FF` | system ID | kept; product number, version, date and title replaced (so Mednafen's game database cannot attach another game's settings); `+0xE0` IP size set to cover the code (rounded up to `0x800`, at most `0x8000`); `+0xE8`/`+0xEC` stacks kept unless zero; `+0xF0` (1st read address) `0x06010000`, `+0xF4` (1st read size) `0` |
| `0x100..0xDFF` | security code (the BIOS runs it at `0x06002100` after loading the IP to `0x06002000`) | byte-identical copy |
| `0xE00 + 0x20*i` | one area-code block per region letter in `+0x40` (`n` letters) | byte-identical copy; each must contain `For ` or the build refuses |
| `0xE00 + 0x20*n` | the application's initial code | the guest: a trampoline that copies the payload to `0x06010000` and jumps there. It never returns and loads nothing itself; `+0xF4` stays `0`, so the BIOS is not asked to load the file below (the setting that booted on the real run). The order of events if a loader did load it is not established, so the file is made harmless instead: its bytes are the first bytes of the payload |

Everything after the guest in the 32 KB system area is zeroed, followed by an ISO 9660 volume whose root
directory holds one file, `V1.BIN;1` (sector 21, the first up-to-2048 bytes of the payload; without any file the
BIOS will not boot the disc) and a `.cue` (32 sectors total). With the IP at `0x06002000` the entry is `0x06002E00 + 0x20*n`; the
trampoline is position independent. The payload always runs at `0x06010000`, so `func_a` / `func_b` and
every other label sit at the same addresses whatever `n` is (they did move when the IMS re-arm was added to the handler; read them from `v1_symbols.json` of the build you run) (`v1_symbols.json` lists them).

`build_v1.py --probe PATH` prints the system ID fields under their documented names, `n` and the detected
area blocks. Not verified: that the BIOS accepts the result and runs the code. If it does not boot, the
first things to look at are the probe output (is `n` what you expect for your image?) and `--ip-load`.
An alternative that was investigated and not implemented: Mednafen's bootable cartridge ROM loader (`.ss`,
`cart/bootrom.cpp`) maps a raw image at `0x02000000`, but how the BIOS chooses to run it is not
documented in the pinned source and could not be tested.

## Other limits

* After `SSHON` the slave runs whatever the BIOS gives it; the slave's busy/sleep split is not asserted.
* `-ss.dbg_cem full` (instruction cache emulation): steady-state figures were identical to the default on the real run; see `EXPECTED.md` "Cache modes".
* The function-attribution phase needs a `--enable-debugger` build and the Functions checkbox.

## Hardware state the fake machine does not model (risk stated)

`tests/sh2sim.py` models: SCU IMask reset to `0xBFFF` on every master vector fetch (`SCU_MSH2VectorFetch`) and
the IMS write path; SMPC SF reading 0 (commands instant); cached versus uncached accesses. It does **not**
model, so these are unchecked risks until a real run:

* SCU `IPending` (IST): pending bits are cleared by the CPU's vector fetch (`CheckDoMasterInt`), so no IST
  write is needed; the guest relies on that. Other pending bits (VDP1 draw end, DMA end) accumulate masked
  and are harmless only while masked.
* SMPC: command execution takes time and `SF` clears later; an INTBACK left in progress by the BIOS (a
  peripheral-continue handshake) could make a new COMREG write misbehave. The guest waits for `SF` clear
  before and after each command but cannot tell an INTBACK continuation from idle.
* SMPC `SSHON` leaves the slave running BIOS code; what it does (and whether it disturbs shared work RAM
  low) is unknown.
* VDP1: drawing starts only at a frame-buffer swap with `PTMR` = 2 and `FBCR` = 0; `EDSR`/`COPR` and the
  draw-end interrupt are not acknowledged by the guest. Whether the BIOS left `TVMR`/erase registers in a
  state that changes the swap is not modelled (the guest rewrites `TVMR`, `FBCR`, `PTMR`).
* SCU DMA: level 0 `D0EN` is assumed to stay set after a transfer (no self-clear in `scu.inc` for
  VBlank-OUT start); the guest zeroes `D1EN`/`D2EN` too but cannot know a BIOS-started transfer is not
  still running at entry.
* Sound: the SCSP's own interrupt enables are not touched; the 68000 programs run with SR = `0x2700` (reset
  value) and `STOP #$2700`, so SCSP interrupts cannot reach them even if the BIOS left them enabled.
* VDP2: `TVMD` takes effect at the next frame; the back-screen colour write assumes VRAM bank settings left by
  the BIOS allow a CPU write at VRAM offset `0x7FFFE`.
