"""CRAM and VDP1 frame-buffer pokes must land in the arrays the snapshot reads back.

Mednafen's CheatMemWrite (what SsDbgPokeByte calls) writes only ranges in SH7095_FastMap, and
neither the CRAM window nor the VDP1 frame buffer is in it, so a poke there was accepted and dropped.
apply.py injects two accessors instead. This compiles them against stubs of the vdp1.cpp / vdp2.cpp
statics they touch -- Write16_DB's CRAM mapping is copied from vdp2.cpp's RW<>() -- and checks that a
byte poke at raw offset 'off' changes exactly that byte of CRAM[] (in every CRAM mode, which is what
the inverse mapping in the accessor exists for) and of the DISPLAYED frame-buffer bank, the one
SsDbgVdp1Fb reads. Usage: check_vdp_poke.py <apply.py> <scratch dir> [c++]"""
import importlib.util
import os
import subprocess
import sys

apply_py, work = sys.argv[1], sys.argv[2]
spec = importlib.util.spec_from_file_location("se_apply", apply_py)
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)


def body(block):
    """The accessor block without its namespace wrapper (the harness supplies the namespace)."""
    lines = block.strip().splitlines()
    return "\n".join(l for l in lines if not l.startswith("namespace") and l != "}}")


HARNESS = r'''
#include <cstdio>
#include <cstdint>
typedef uint8_t uint8; typedef uint16_t uint16;
namespace MDFN_IEN_SS {
template<typename T> static void ne16_wbo_be(uint16* base, uint32_t byte_off, T v)
{   /* Mednafen's helper: big-endian byte order inside each 16-bit word */
    const unsigned sh = ((byte_off & 1) ^ 1) * 8;
    uint16& w = base[byte_off >> 1];
    w = (uint16)((w & ~(0xFF << sh)) | ((unsigned)v << sh));
}
namespace VDP1 {
static bool FBDrawWhich;
uint16 FB[2][0x20000];
#include "vdp1_poke.inc"
}
namespace VDP2 {
enum { CRAM_MODE_RGB555_1024 = 0, CRAM_MODE_RGB555_2048 = 1, CRAM_MODE_RGB888_1024 = 2, CRAM_MODE_ILLEGAL = 3 };
static uint8 CRAM_Mode;
static uint16 CRAM[2048];
/* The bus write, as vdp2.cpp's RW<uint16, true>() does it for the CRAM window. */
void Write16_DB(uint32_t A, uint16 DB)
{
    A &= 0x1FFFFF;
    if (A >= 0x100000 && A < 0x180000)
    {
        const unsigned cri = (A & 0xFFF) >> 1;
        switch (CRAM_Mode)
        {
        case CRAM_MODE_RGB555_1024: (CRAM + 0x000)[cri & 0x3FF] = DB; (CRAM + 0x400)[cri & 0x3FF] = DB; break;
        case CRAM_MODE_RGB555_2048: CRAM[cri] = DB; break;
        default: CRAM[((cri >> 1) & 0x3FF) | ((cri & 1) << 10)] = DB; break;
        }
    }
}
#include "vdp2_poke.inc"
}
}
using namespace MDFN_IEN_SS;
namespace { int bad = 0; }
#define CHECK(c, m) do { if (!(c)) { printf("FAIL: %s\n", m); bad++; } } while (0)
extern "C" void SsDbgPokeCramByte(unsigned int off, unsigned char val);
extern "C" void SsDbgPokeVdp1FbByte(unsigned int off, unsigned char val);
int main()
{
    /* Frame buffer: the displayed bank takes the byte, the draw bank is untouched. */
    for (int draw = 0; draw < 2; ++draw)
    {
        VDP1::FBDrawWhich = draw != 0;
        VDP1::FB[0][3] = VDP1::FB[1][3] = 0;
        SsDbgPokeVdp1FbByte(6, 0xAB);   /* even offset = high byte of word 3 */
        SsDbgPokeVdp1FbByte(7, 0xCD);
        CHECK(VDP1::FB[!VDP1::FBDrawWhich][3] == 0xABCD, "VDP1 FB byte pokes land big-endian in the displayed bank");
        CHECK(VDP1::FB[VDP1::FBDrawWhich][3] == 0, "and leave the draw bank alone");
    }
    VDP1::FB[0][0x1FFFF] = 0;
    VDP1::FBDrawWhich = true;
    SsDbgPokeVdp1FbByte(0x3FFFF + 0x40000, 0x5A);   /* past the bank wraps inside it, never out of the array */
    CHECK(VDP1::FB[0][0x1FFFF] == 0x005A, "an offset past the bank is masked into it");

    /* CRAM: every raw word, every mode. */
    for (int mode = 0; mode < 4; ++mode)
    {
        VDP2::CRAM_Mode = (uint8)mode;
        for (unsigned w = 0; w < 2048; ++w)
        {
            for (unsigned i = 0; i < 2048; ++i) VDP2::CRAM[i] = (uint16)(0x1000 + i);
            const uint16 before = VDP2::CRAM[w];
            SsDbgPokeCramByte(w * 2 + 1, 0x77);   /* low byte */
            if (VDP2::CRAM[w] != (uint16)((before & 0xFF00) | 0x77)) { printf("FAIL: mode %d word %u low byte -> %04X\n", mode, w, VDP2::CRAM[w]); ++bad; break; }
            SsDbgPokeCramByte(w * 2, 0x99);       /* high byte */
            if ((VDP2::CRAM[w] >> 8) != 0x99) { printf("FAIL: mode %d word %u high byte\n", mode, w); ++bad; break; }
            if (mode == 1 || mode >= 2)
            {
                for (unsigned i = 0; i < 2048; ++i)
                    if (i != w && VDP2::CRAM[i] != (uint16)(0x1000 + i)) { printf("FAIL: mode %d poke of word %u also changed word %u\n", mode, w, i); ++bad; w = 2048; break; }
            }
        }
    }
    return bad ? 1 : 0;
}
'''
os.makedirs(work, exist_ok=True)
open(os.path.join(work, "vdp1_poke.inc"), "w").write(body(mod.VDP1_POKE_ACCESSORS))
open(os.path.join(work, "vdp2_poke.inc"), "w").write(body(mod.VDP2_POKE_ACCESSORS))
src = os.path.join(work, "vdp_poke.cpp")
open(src, "w").write(HARNESS)
exe = os.path.join(work, "vdp_poke")
cc = subprocess.run([sys.argv[3] if len(sys.argv) > 3 else "g++", "-std=c++17", "-Wall", "-I", work, src, "-o", exe],
                    capture_output=True, text=True)
if cc.returncode:
    print(cc.stderr)
    sys.exit(2)
run = subprocess.run([exe], capture_output=True, text=True)
print(run.stdout, end="")
sys.exit(run.returncode)
