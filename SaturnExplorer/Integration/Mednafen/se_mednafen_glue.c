/* Saturn Explorer — Mednafen (Beetle Saturn) live-tap glue.  *** TEMPLATE ***
 *
 * This is the emulator-specific half of the live tap. The portable half is
 * ../Common/se_export.c (unchanged). Drop this + se_export.{c,h} + SeLiveProtocol.h
 * into Mednafen's ss core, then wire the calls at the bottom into ss.cpp's frame
 * loop. Spots that need Mednafen's actual symbol names are marked TODO(mednafen).
 *
 * It does NOT compile as-is: the `extern` block below references Mednafen `ss`
 * internals that only exist inside a Mednafen build. Everything else — the
 * byte-order swap, the VDP2 struct rebuild, the SH-2 register packing — is real and
 * emulator-independent, and is the part worth getting right here.
 *
 * See README.md for the full symbol map, byte-order rules, and phasing.
 */
#include "se_export.h"
#include "SeLiveProtocol.h"

#include <string.h>
#include <stdint.h>

/* ---- Mednafen ss symbols this glue reads. Confirm names against your source
 *      (mednafen/src/ss/*). These are the likely targets; adjust as needed. ---- */
#if 0  /* TODO(mednafen): remove the guard once wired against real ss headers. */
extern uint16_t VDP1_VRAM[0x40000 / 2];      /* VDP1::VRAM  — 512 KiB, host order  */
extern uint16_t VDP2_VRAM[0x80000 / 2];      /* VDP2::VRAM  — 512 KiB, host order  */
extern uint16_t VDP2_CRAM[0x1000 / 2];       /* VDP2::CRAM  — 4 KiB,  host order   */
extern uint16_t VDP2_RawRegs[0x100];         /* VDP2 regs, indexed by (hw >> 1)    */
extern uint8_t  WorkRAML[0x100000];          /* low work RAM  @ 0x00200000         */
extern uint8_t  WorkRAMH[0x100000];          /* high work RAM @ 0x06000000         */
extern const uint16_t* SS_VDP1DisplayFB(void); /* displayed VDP1 FB bank (RGB555)  */
/* SH-2 cores. Mednafen's SH7095 exposes registers; adapt the accessor below. */
typedef struct SH7095 SH7095;
extern SH7095 CPU[2];                        /* [0]=master, [1]=slave              */
extern void   SS_GetSH2Regs(const SH7095*, uint32_t out23[23]); /* R[16],SR,GBR,VBR,MACH,MACL,PR,PC */
extern void   SS_PokeByte(uint32_t addr, uint8_t val);          /* bus/debug byte write */
#endif

/* ============================ pure, testable helpers ====================== */

/* VRAM arrives host-order (LE 16-bit words); the wire wants Saturn big-endian.
 * Swap each 16-bit word from 'src' into 'dst' (both 'len' bytes). */
static void SwapU16ToBE(uint8_t* dst, const uint8_t* src, size_t len)
{
    size_t i;
    for (i = 0; i + 1 < len; i += 2)
    {
        dst[i]     = src[i + 1];
        dst[i + 1] = src[i];
    }
}

/* Rebuild the raw 288-byte Yabause `Vdp2` struct the client expects, from
 * Mednafen's flat RawRegs[0x100] (register at hw offset o is RawRegs[o>>1]).
 * The struct is a re-layout of the same registers via a fixed hardware map; write
 * each register little-endian at its struct offset so the client's
 * sedrv::BuildVdp2RegImage reads it back correctly.
 *
 * kVdp2RegStructOffset is the SAME 144-entry table the client uses — copy it
 * verbatim from Drivers/Common/src/SaturnStateShared.cpp (0xFFFF = reserved slot).
 * (Longer term, prefer protocol v7: send a pre-built hw-offset BE image and drop
 *  this rebuild entirely — see README "VDP2 registers".) */
static void BuildYabauseVdp2Struct(uint8_t out288[288], const uint16_t rawRegs[0x100])
{
    /* TODO(mednafen): paste kVdp2RegStructOffset[144] from SaturnStateShared.cpp. */
    extern const uint16_t kVdp2RegStructOffset[144];
    unsigned hw;
    memset(out288, 0, 288);
    for (hw = 0; hw <= 0x11E; hw += 2)
    {
        const uint16_t so = kVdp2RegStructOffset[hw >> 1];
        if (so == 0xFFFF || (unsigned)(so + 1) >= 288) continue;
        const uint16_t v = rawRegs[hw >> 1];
        out288[so]     = (uint8_t)(v & 0xFF);   /* little-endian, as the client reads */
        out288[so + 1] = (uint8_t)(v >> 8);
    }
}

/* Assemble the hardware-offset, big-endian VDP1 register image (0x18 bytes) the
 * wire expects. Mednafen exposes VDP1's control/status registers individually
 * (TVMR, FBCR, PTMR, EWDR, EWLR, EWRR, EDSR, LOPR) — write each at its hw offset,
 * big-endian; ENDR/COPR/MODR are write-only/computed and stay zero. See
 * BuildVdp1RegImageFromMednafen in SavestateDriver.cpp for the exact field→offset map. */
static void BuildVdp1RegImage(uint8_t out24[0x18])
{
    memset(out24, 0, 0x18);
    /* TODO(mednafen): out24[hw]=reg>>8; out24[hw+1]=reg&0xFF; for each VDP1 reg. */
}

/* Pack one Mednafen SH-2 core's registers into the wire's sh2regs_struct order
 * (23 host-order u32: R[0..15], SR, GBR, VBR, MACH, MACL, PR, PC). */
static void PackSh2(uint32_t out23[23] /*, const SH7095* cpu */)
{
    memset(out23, 0, 23 * sizeof(uint32_t));
    /* TODO(mednafen): SS_GetSH2Regs(cpu, out23); — or fill fields directly:
     *   out23[0..15] = R[0..15]; out23[16]=SR; [17]=GBR; [18]=VBR;
     *   out23[19]=MACH; out23[20]=MACL; out23[21]=PR; out23[22]=PC; */
}

/* ============================ per-frame snapshot ========================== */

/* Call once per emulated frame, after the frame is rendered (Mednafen ss
 * Emulate(), post-VDP2). Builds the wire-ready buffers and hands them to the
 * portable server. For Tier 1 (read-only view) you may pass NULL for work RAM and
 * the SH-2 args — those sections then ship empty and the client no-ops them. */
void SeMednafenSnapshot(void)
{
    static uint8_t v1[SE_LIVE_VDP1_VRAM_LEN], v2[SE_LIVE_VDP2_VRAM_LEN];
    static uint8_t vs[SE_LIVE_VDP2_STRUCT_LEN], vr[SE_LIVE_VDP1_REGS_LEN];
    static uint32_t msh2[23], ssh2[23];

#if 0  /* TODO(mednafen): enable once the extern block is wired. */
    SwapU16ToBE(v1, (const uint8_t*)VDP1_VRAM, sizeof v1);   /* -> big-endian */
    SwapU16ToBE(v2, (const uint8_t*)VDP2_VRAM, sizeof v2);
    BuildYabauseVdp2Struct(vs, VDP2_RawRegs);
    BuildVdp1RegImage(vr);
    PackSh2(msh2 /*, &CPU[0] */);
    PackSh2(ssh2 /*, &CPU[1] */);

    SeExportSnapshot(
        v1,                          /* VDP1 VRAM  (big-endian)                 */
        v2,                          /* VDP2 VRAM  (big-endian)                 */
        (const void*)VDP2_CRAM,      /* CRAM       (host order — client normalizes) */
        vs,                          /* VDP2 regs  (raw Yabause struct)         */
        NULL,                        /* VDP1 regs struct — we pass the image via vr:
                                        se_export builds from a struct; if your build
                                        wants the ready image instead, see note below */
        WorkRAML,                    /* low work RAM  (host order; verify)      */
        WorkRAMH,                    /* high work RAM (host order; verify)      */
        (const void*)SS_VDP1DisplayFB(), /* VDP1 framebuffer (RGB555)           */
        msh2, ssh2);                 /* SH-2 master + slave                     */
#else
    (void)v1; (void)v2; (void)vs; (void)vr; (void)msh2; (void)ssh2;
    (void)SwapU16ToBE; (void)BuildYabauseVdp2Struct; (void)BuildVdp1RegImage; (void)PackSh2;
#endif
}

/* NOTE on VDP1 regs: se_export's SeExportSnapshot builds the VDP1 image from a
 * Yabause `Vdp1` *struct* (SeBuildVdp1Image). Mednafen has no such struct, so build
 * the image here (BuildVdp1RegImage) and, in the copy of se_export.c you ship with
 * Mednafen, pass `vr` through unchanged instead of rebuilding — or (cleaner) switch
 * VDP1 regs to the same pre-built-image convention proposed for VDP2 in the README. */

/* ============================ debugger bridge (Tier 3) ==================== */
/* Wire these to Mednafen's ss debug API, then register them once at init with
 * SeExportSetBreakpointHooks(SeMdfnAddExecBp, SeMdfnClearBps) and
 * SeExportSetMemWriteHook(SeMdfnWriteByte). Call SeExportNotifyStop(cpu, pc) from
 * Mednafen's breakpoint callback. */
static void SeMdfnAddExecBp(int cpu, unsigned int address)
{
    (void)cpu; (void)address;
    /* TODO(mednafen): install an execution breakpoint on CPU[cpu] at 'address'. */
}
static void SeMdfnClearBps(void)
{
    /* TODO(mednafen): remove all execution breakpoints on both SH-2 cores. */
}
static void SeMdfnWriteByte(unsigned int address, unsigned char value)
{
    (void)address; (void)value;
    /* TODO(mednafen): SS_PokeByte(address, value);  — big-endian preserved by
       writing byte-by-byte at Saturn addresses, no manual swap. */
}

/* ============================ lifecycle wiring ============================ */
/* Wire these into Mednafen ss (names per your source):
 *   - at ss init (Load):      SeExportInit();
 *                             SeExportSetBreakpointHooks(SeMdfnAddExecBp, SeMdfnClearBps);
 *                             SeExportSetMemWriteHook(SeMdfnWriteByte);
 *   - once per frame (Emulate, post-render):   SeMednafenSnapshot();
 *   - top of Emulate (optional pause/step):    while (!SeExportGateFrame()) { }
 *       (Tier-1 read-only view can skip the gate; see README frame-gate caveat.)
 *   - at ss CloseGame:        SeExportDeinit();
 */
void SeMednafenSuppressUnusedWarnings(void)
{
    (void)SeMdfnAddExecBp; (void)SeMdfnClearBps; (void)SeMdfnWriteByte; (void)SeMednafenSnapshot;
}
