"""The Mednafen patch's VDP1 draw-end latch must not outlive the VRAM it copied.

The latch is a copy of VDP1 VRAM that the glue prefers over live VRAM. Mednafen restores VRAM on a
state load and rewrites it on a reset without knowing about the copy, so a latch left valid keeps
exporting the abandoned timeline (command list, VRAM Map, reconstructed output) until the next
draw-end. This compiles the injected accessor block against a stub VRAM and checks that a load/reset
invalidation sends the glue back to live VRAM, then checks that the hooks that call it find their
anchors in the shape of Mednafen's vdp1.cpp. Usage: check_vdp1_latch.py <apply.py> <scratch dir> [c++]"""
import importlib.util
import os
import subprocess
import sys

apply_py, work = sys.argv[1], sys.argv[2]
spec = importlib.util.spec_from_file_location("se_apply", apply_py)
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)

HARNESS = r'''
#include <cstdio>
#include <cstring>
namespace MDFN_IEN_SS { namespace VDP1 {
static unsigned short VRAM[0x40000];
static unsigned short FB[2][0x20000]; static int FBDrawWhich;
static unsigned short TVMR, FBCR, PTMR, EWDR, EWLR, EWRR, EDSR, LOPR;
#include "accessors.inc"
}}
extern "C" const unsigned short* SsDbgVdp1Latch(void);
extern "C" void SsDbgVdp1LatchDrawEnd(void);
extern "C" void SsDbgVdp1LatchInvalidate(void);
namespace { int bad = 0; }
#define CHECK(c, m) do { if (!(c)) { printf("FAIL: %s\n", m); bad++; } } while (0)
int main()
{
 /* The glue's choice: latch if valid, else live VRAM. Reproduced from SeMednafen... the accessor
    returns NULL exactly when the glue must use live VRAM. */
 CHECK(SsDbgVdp1Latch() == 0, "fresh boot should have no latch");
 MDFN_IEN_SS::VDP1::VRAM[0] = 0x1111;
 SsDbgVdp1LatchDrawEnd();
 CHECK(SsDbgVdp1Latch() && SsDbgVdp1Latch()[0] == 0x1111, "draw-end should latch");
 /* A state load restores VRAM behind the latch's back. */
 MDFN_IEN_SS::VDP1::VRAM[0] = 0x2222;
 SsDbgVdp1LatchInvalidate();
 CHECK(SsDbgVdp1Latch() == 0, "latch must be dropped after a load/reset");
 SsDbgVdp1LatchDrawEnd();
 CHECK(SsDbgVdp1Latch() && SsDbgVdp1Latch()[0] == 0x2222, "the next draw-end latches the new timeline");
 return bad ? 1 : 0;
}
'''
os.makedirs(work, exist_ok=True)
open(os.path.join(work, "accessors.inc"), "w").write(
    mod.VDP1_ACCESSORS.replace('namespace MDFN_IEN_SS { namespace VDP1 {', '').rstrip().rstrip('}'))
src = os.path.join(work, "latch.cpp")
open(src, "w").write(HARNESS)
exe = os.path.join(work, "latch")
cc = subprocess.run([sys.argv[3] if len(sys.argv) > 3 else "g++", "-std=c++17", "-I", work, src, "-o", exe],
                    capture_output=True, text=True)
if cc.returncode:
    print(cc.stderr)
    sys.exit(2)
run = subprocess.run([exe], capture_output=True, text=True)
print(run.stdout, end="")
bad = run.returncode

# The hooks must find their anchors in Mednafen's vdp1.cpp shape (the real file is not in this repo).
VDP1_SHAPE = """\
void Reset(bool powering_up)
{
 if(powering_up)
 {
 }
}

void StateAction(StateMem* sm, const unsigned load, const bool data_only)
{
 SFORMAT StateRegs[] = { SFEND };

 MDFNSS_StateAction(sm, load, data_only, StateRegs, "VDP1");

 if(load)
 {
 }
}
"""
text = VDP1_SHAPE
text, n1 = mod.apply_anchored(text, mod.VDP1_RESET_ANCHOR, mod.VDP1_RESET_HOOK, "SE_VDP1_LATCH_RESET")
text, n2 = mod.apply_anchored(text, mod.VDP1_LOAD_ANCHOR, mod.VDP1_LOAD_HOOK, "SE_VDP1_LATCH_LOAD")
for n in (n1, n2):
    if "applied" not in n:
        print("FAIL: " + n.strip())
        bad = 1
reset_body = text[text.index("void Reset"):text.index("if(powering_up)")]
load_body = text[text.index('"VDP1");'):text.index("if(load)\n {")]
if "SsDbgVdp1LatchInvalidate();" not in reset_body:
    print("FAIL: Reset does not invalidate the latch")
    bad = 1
if "SsDbgVdp1LatchInvalidate();" not in load_body:
    print("FAIL: the VDP1 state load does not invalidate the latch")
    bad = 1
sys.exit(1 if bad else 0)
