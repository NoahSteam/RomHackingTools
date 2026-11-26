#!/bin/sh
# Build the V1 validation disc and print how to run it. Does not start anything itself.
#
#   run_v1.sh /path/to/IP.BIN-or-any-Saturn-disc-image [outdir]
#
# Needs only python3. The first argument must be a file you own; only its licensing block and area
# header bytes are copied (see build_v1.py). Windows: run the same python command from a terminal
# (see run_v1.bat.txt), nothing here is POSIX-specific except this wrapper.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
src=${1:-}
out=${2:-"$here/out"}
if [ -z "$src" ]; then
    echo "usage: $0 IP.BIN|disc-image [outdir]" >&2
    exit 2
fi
python3 -I "$here/build_v1.py" --probe "$src"
python3 -I "$here/build_v1.py" --ip-bin "$src" --out "$out"
cat <<EOF

Built: $out/v1.cue  (and v1.iso, v1_symbols.json: func_a / func_b entry addresses)

1. Start the PATCHED Mednafen (the build install.py made, with --enable-debugger if you want the
   function check, phase 2) on the cue. Default cache mode first:
       mednafen -force_module ss "$out/v1.cue"
   Second pass, instruction cache emulated (EXPECTED.md "Cache modes"):
       mednafen -force_module ss -ss.dbg_cem full "$out/v1.cue"
   (Or use Saturn Explorer's Launch menu with the cue; put -ss.dbg_cem full in the launch arguments
   for the second pass.) The window title must say "SaturnExplorer Enabled".

2. Connect Saturn Explorer:  SaturnExplorer --live   (or File > Connect to emulator (live)).
   Windows > Debugger > Profiler. For phase 2 tick the Functions checkbox (needs --enable-debugger).

3. The screen shows a solid back-screen colour per phase and the guest cycles through 14 phases of
   120 frames (about 28 s at 60 Hz) forever. Read each phase in its steady frames (frames 5..118,
   a 16-frame window in the middle). Colour -> phase -> what to read:

   colour       id name        read in the Profiler
   black        0  IDLE        Frame tab: master sleep share, slave on-time, 68000 idle/executed, Buses tab baseline
   red          1  BUSY        Frame tab: master sleep / busy cycles
   green        2  FUNC        Functions tab, master: two rows (func_a, func_b); ratio
   blue         3  BUS         Buses tab: per-device request counts (cache mode shown there)
   yellow       4  VDP1_ONE    Frame tab: VDP1 commands, starts, command work
   cyan         5  VDP1_MANY   same
   magenta      6  DMA_BIG     Buses tab: DMA level 0 transfers/bytes/route; Frame tab: master stall
   grey         7  DMA_SMALL   same
   dark         8  DMA_DRAW    VDP1 command work + DMA-discarded budget + DMA
   0x5294       9  SSH_ON      slave on-time (first frame partial, then full frame)
   0x35AD      10  SSH_OFF     slave on-time (first frame partial, then 0)
   0x1CE7      11  M68K_STOP   68000 idle vs executed
   0x739C      12  M68K_BUSY   68000 idle vs executed
   0x0842      13  M68K_OFF    68000 idle vs executed

   Live phase id and frame count: work RAM high 0x0600FF00 and 0x0600FF04 (Memory view).

4. Record every reading in CHECKLIST.md next to the expected value; EXPECTED.md has the derivations.

The disc layout comes from the documented system ID format and has not been run against a BIOS.
If it does not boot (black screen, BIOS menu, "not a Saturn disc"): see "How it boots, and the
limits" in README.md in this directory, and check the --probe output above (area blocks, n).
EOF
