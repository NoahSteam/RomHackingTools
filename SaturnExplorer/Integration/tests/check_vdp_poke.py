"""CRAM, VDP1 frame-buffer and VDP2 VRAM pokes must change the byte the client next reads back.

Mednafen's CheatMemWrite (what SsDbgPokeByte calls) writes only ranges in SH7095_FastMap, and
neither the CRAM window nor the VDP1 frame buffer is in it, so a poke there was accepted and dropped.
apply.py injects accessors instead. This compiles them against stubs of the vdp1.cpp / vdp2.cpp
statics they touch -- Write16_DB's CRAM mapping is copied from vdp2.cpp's RW<>() -- and checks the
round trip a user sees: poke byte 'off', capture the way the glue does, normalize the way the client
does (sedrv::NormalizeCramToBigEndian, se_mednafen_glue.c's SwapU16ToBE), and only byte 'off' has
changed. Checking the raw arrays alone passed while RGB888 CRAM and the frame buffer came back with a
different byte changed. It also checks that SsDbgPokeByte hands a VDP2 VRAM poke to PokeVRAM, the
only writer that reaches the renderer's copy of VRAM.
Usage: check_vdp_poke.py <apply.py> <scratch dir> [c++]"""
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
/* SsDbgPokeByte's world: the bus writer and the renderer-aware VRAM writer, recording calls. */
static unsigned gCheatA, gCheatN, gPokeA, gPokeN, gLatchOff, gLatchN;
static void CheatMemWrite(unsigned A, uint8) { gCheatA = A; ++gCheatN; }
namespace VDP2 { static void PokeVRAM(uint32_t A, uint8) { gPokeA = A; ++gPokeN; } }
extern "C" void SsDbgVdp1LatchPokeByte(unsigned int off, unsigned char) { gLatchOff = off; ++gLatchN; }
#include "ss_poke.inc"
}
using namespace MDFN_IEN_SS;
namespace { int bad = 0; }
#define CHECK(c, m) do { if (!(c)) { printf("FAIL: %s\n", m); bad++; } } while (0)
extern "C" void SsDbgPokeCramByte(unsigned int off, unsigned char val);
extern "C" void SsDbgPokeVdp1FbByte(unsigned int off, unsigned char val);
/* The client's view of CRAM after a capture: the glue's wire bytes, then NormalizeCramToBigEndian. */
static void CaptureCram(uint8 out[4096])
{
    VDP2::SsDbgCramWire(out);
    const unsigned step = VDP2::CRAM_Mode == 2 ? 4 : 2;
    for (unsigned i = 0; i < 4096; i += step)
        for (unsigned a = 0, b = step - 1; a < b; ++a, --b) { uint8 t = out[i + a]; out[i + a] = out[i + b]; out[i + b] = t; }
}
/* The client's view of the displayed FB bank: host words swapped to big-endian (on a little-endian host). */
static uint8 CaptureFbByte(unsigned off)
{
    const uint16 w = VDP1::FB[!VDP1::FBDrawWhich][off >> 1];
    return (uint8)((off & 1) ? w : (w >> 8));
}
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

    /* Frame buffer round trip: each byte of an asymmetric word reads back where it was poked. */
    for (int draw = 0; draw < 2; ++draw)
    {
        VDP1::FBDrawWhich = draw != 0;
        VDP1::FB[0][0] = VDP1::FB[1][0] = 0;
        SsDbgPokeVdp1FbByte(0, 0x12);
        CHECK(CaptureFbByte(0) == 0x12 && CaptureFbByte(1) == 0x00, "FB even byte reads back at its own offset");
        SsDbgPokeVdp1FbByte(1, 0x34);
        CHECK(CaptureFbByte(0) == 0x12 && CaptureFbByte(1) == 0x34, "FB odd byte reads back at its own offset");
    }

    /* CRAM round trip: every byte, every mode -- poke, capture, normalize, and only that byte moved. */
    for (int mode = 0; mode < 4; ++mode)
    {
        VDP2::CRAM_Mode = (uint8)mode;
        /* RGB555 1024 mirrors the two halves; its bus window is the first 2 KiB. */
        const unsigned span = mode == 0 ? 2048 : 4096;
        static uint8 before[4096], after[4096];
        for (unsigned off = 0; off < span; ++off)
        {
            for (unsigned i = 0; i < 2048; ++i) VDP2::CRAM[i] = (uint16)(0x1000 + i * 7);
            if (mode == 0) for (unsigned i = 0; i < 1024; ++i) VDP2::CRAM[0x400 + i] = VDP2::CRAM[i];
            CaptureCram(before);
            const uint8 v = (uint8)(before[off] ^ 0xA5);
            SsDbgPokeCramByte(off, v);
            CaptureCram(after);
            bool ok = after[off] == v;
            for (unsigned i = 0; i < span && ok; ++i) if (i != off && after[i] != before[i]) ok = false;
            if (!ok) { printf("FAIL: mode %d CRAM byte %u does not round-trip\n", mode, off); ++bad; break; }
        }
    }
    /* The reported case: RGB888, zero CRAM, AB at offset 0 reads back AB 00 00 00. */
    VDP2::CRAM_Mode = 2;
    for (unsigned i = 0; i < 2048; ++i) VDP2::CRAM[i] = 0;
    SsDbgPokeCramByte(0, 0xAB);
    {
        uint8 c[4096];
        CaptureCram(c);
        CHECK(c[0] == 0xAB && c[1] == 0 && c[2] == 0 && c[3] == 0, "RGB888 byte 0 reads back as AB 00 00 00");
    }

    /* VDP2 VRAM (and its mirrors, at any cache-area alias) also goes to PokeVRAM; nothing else does. */
    const unsigned vram[] = { 0x05E00000u, 0x25E7FFFFu, 0x05EFFFFFu };
    for (unsigned a : vram)
    {
        gPokeN = 0;
        SsDbgPokeByte(a, 1);
        CHECK(gPokeN == 1 && gPokeA == (a & 0x07FFFFFFu), "a VDP2 VRAM poke reaches the renderer's copy");
    }
    const unsigned other[] = { 0x06000000u, 0x05C00000u, 0x05F00000u, 0x25A00000u, 0x05DFFFFFu };
    for (unsigned a : other)
    {
        gPokeN = 0; gCheatN = 0;
        SsDbgPokeByte(a, 1);
        CHECK(gPokeN == 0 && gCheatN == 1, "other regions go to the bus writer only");
    }
    /* VDP1 VRAM (any alias) also reaches the draw-end latch the snapshot shows; nothing else does. */
    const unsigned v1[] = { 0x05C00000u, 0x25C7FFFFu };
    for (unsigned a : v1)
    {
        gLatchN = 0;
        SsDbgPokeByte(a, 1);
        CHECK(gLatchN == 1 && gLatchOff == ((a & 0x07FFFFFFu) - 0x05C00000u), "a VDP1 VRAM poke reaches the latch");
    }
    const unsigned notV1[] = { 0x05C80000u, 0x05BFFFFFu, 0x05E00000u };
    for (unsigned a : notV1)
    {
        gLatchN = 0;
        SsDbgPokeByte(a, 1);
        CHECK(gLatchN == 0, "nothing outside VDP1 VRAM reaches the latch");
    }

    return bad ? 1 : 0;
}
'''
os.makedirs(work, exist_ok=True)
open(os.path.join(work, "vdp1_poke.inc"), "w").write(body(mod.VDP1_POKE_ACCESSORS))
open(os.path.join(work, "vdp2_poke.inc"), "w").write(body(mod.VDP2_POKE_ACCESSORS))
# Just SsDbgPokeByte out of ss.cpp's accessor block; the rest needs the whole emulator.
ss = mod.SS_ACCESSORS
fn = ss.index('extern "C" void SsDbgPokeByte(')
open(os.path.join(work, "ss_poke.inc"), "w").write(ss[fn:ss.index("\n}\n", fn) + 3])
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
