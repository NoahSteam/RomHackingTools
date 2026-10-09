"""The Mednafen patch's halt paths must raise sSeHalted BEFORE publishing the halted snapshot.

SeMednafenFrameHook() applies queued installs, and an install calls SeSyncCpuHook(), which tells the
glue the CPU is entering an unwatched run unless sSeHalted says it is halted. Raised after the
snapshot, a breakpoint edit made while halted dropped the held call/return (a halted `rts` lost its
frame before it ran; a Step Into from a `bsr` lost the call). The shadow-stack tests drive the glue
directly, so they cannot see this ordering; check the injected source."""
import re
import sys

src = open(sys.argv[1]).read()
bad = 0
# Each halt path: the statement that raises the flag must come before the snapshot call, within the
# same function (no other function boundary between them).
for name, start in (("SeSsBpHook", "static void SeSsBpHook(uint32 PC, bool bpoint) {"),
                    ("SeSsDmaWatch", 'extern "C" void SeSsDmaWatch(')):
    i = src.index(start)
    j = src.index("while (!SeExportGateHalt())", i)
    body = src[i:j]
    code = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
    code = re.sub(r"//[^\n]*", "", code)
    flag, snap = code.find("sSeHalted = 1;"), code.find("SeMednafenFrameHook();")
    if flag < 0 or snap < 0 or flag > snap:
        print("FAIL: %s raises sSeHalted after the halted snapshot (or not at all)" % name)
        bad += 1
sys.exit(1 if bad else 0)
