/* Saturn Explorer — Mednafen (Beetle Saturn) live-tap glue.  *** TEMPLATE ***
 *
 * This is the emulator-specific half of the live tap. The portable half is
 * ../Common/se_export.c (unchanged). Drop this + se_export.{c,h} + SeLiveProtocol.h
 * into Mednafen's ss core, then wire the calls at the bottom into ss.cpp's frame
 * loop. Symbol names below were mapped against libretro-mirrors/mednafen-git
 * src/ss (see README.md); anything still build-specific is marked TODO(mednafen).
 *
 * Why accessors, not `extern`. Unlike Yabause (extern globals), Mednafen keeps the
 * memory it exposes as *file-scope static*: VDP2::VRAM / CRAM / RawRegs live in
 * vdp2.cpp, VDP1::VRAM / FB / regs in vdp1.cpp, WorkRAML/H + CPU[] in ss.cpp — none
 * reachable by `extern` from this separate translation unit. So the patcher injects
 * a handful of tiny C-linkage accessors into those files (where the statics ARE
 * visible; Mednafen already does this with PeekVRAM / "RawRegs // For debugging")
 * and this glue calls them. The accessor bodies are listed in README.md §"Accessors".
 *
 * The snapshot path is fenced under `SE_MEDNAFEN_WIRED`: undefined (the default) it
 * compiles to a stub, so this file builds anywhere; define it once the injected ss
 * accessors exist and SeMednafenSnapshot() runs for real. (Saturn Explorer's own
 * mdfn_live_e2e test defines it to exercise the transforms in-repo.) The pure
 * transforms — the VRAM host->big-endian swap and the RawRegs->Vdp2-struct rebuild —
 * are real and emulator-independent, and are the part worth getting right here.
 */
#include "se_export.h"
#include "SeLiveProtocol.h"

#include <string.h>
#include <stdint.h>

/* ---- Accessors the patcher injects into Mednafen ss (C linkage). Each reaches a
 *      file-scope static the glue can't see directly. Pointer accessors return the
 *      live array; the two packing accessors run Mednafen-side because they touch
 *      class/static members (VDP1's individual regs, SH7095's register file). ---- */
#if defined(SE_MEDNAFEN_WIRED)  /* define once the injected ss accessors exist (README §Accessors). */
extern const uint16_t* SsDbgVdp1Vram(void);   /* VDP1::VRAM    — 0x40000 words, host order */
extern const uint16_t* SsDbgVdp2Vram(void);   /* VDP2::VRAM    — 262144 words, host order  */
extern const uint16_t* SsDbgCram(void);       /* VDP2::CRAM    — 2048 words,  host order   */
extern const uint16_t* SsDbgRawRegs(void);    /* VDP2::RawRegs — 0x100, indexed (hw>>1)    */
extern const uint16_t* SsDbgWramL(void);      /* WorkRAML      — 0x80000 words @ 0x00200000 */
extern const uint16_t* SsDbgWramH(void);      /* WorkRAMH      — 0x80000 words @ 0x06000000 */
extern const uint16_t* SsDbgVdp1Fb(void);     /* displayed VDP1 FB bank = FB[!FBDrawWhich]  */
extern void            SsDbgVdp1Regs(uint16_t out11[11]); /* TVMR,FBCR,PTMR,EWDR,EWLR,EWRR,ENDR,EDSR,LOPR,COPR,MODR */
extern void            SsDbgSh2Regs(int cpu, uint32_t out23[23]); /* R[16],SR,GBR,VBR,MACH,MACL,PR,PC */
extern void            SsDbgPokeByte(uint32_t addr, uint8_t val); /* Tier 3: bus/debug byte write */
#endif

/* ============================ pure, testable helpers ====================== */

/* VDP1/VDP2 VRAM is stored host-order (native uint16); the wire wants Saturn
 * big-endian. Swap each 16-bit word from 'src' into 'dst' (both 'len' bytes). */
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

/* ============================ per-frame snapshot ========================== */

/* Call once per emulated frame, at the end-of-frame hook in ss.cpp's Emulate()
 * (after `espec->MasterCycles = ...`, before SMPC_UpdateOutput()). Builds the
 * wire-ready buffers and hands them to the portable server. For Tier 1 (read-only
 * view) you may pass NULL for work RAM and the SH-2 args — those sections then ship
 * empty and the client no-ops them. */
void SeMednafenSnapshot(void)
{
    static uint8_t  v1[SE_LIVE_VDP1_VRAM_LEN], v2[SE_LIVE_VDP2_VRAM_LEN];
    static uint8_t  vs[SE_LIVE_VDP2_STRUCT_LEN];
    static uint16_t vdp1[11];
    static uint32_t msh2[23], ssh2[23];

#if defined(SE_MEDNAFEN_WIRED)  /* enabled once the injected accessors exist. */
    SwapU16ToBE(v1, (const uint8_t*)SsDbgVdp1Vram(), sizeof v1);   /* -> big-endian */
    SwapU16ToBE(v2, (const uint8_t*)SsDbgVdp2Vram(), sizeof v2);
    BuildYabauseVdp2Struct(vs, SsDbgRawRegs());
    SsDbgVdp1Regs(vdp1);
    SsDbgSh2Regs(0, msh2);
    SsDbgSh2Regs(1, ssh2);

    SeExportSnapshot(
        v1,                          /* VDP1 VRAM  (big-endian)                 */
        v2,                          /* VDP2 VRAM  (big-endian)                 */
        (const void*)SsDbgCram(),    /* CRAM       (host order — client normalizes) */
        vs,                          /* VDP2 regs  (raw Yabause struct)         */
        vdp1,                        /* VDP1 regs  (11-u16 Yabause struct;      */
                                     /*  se_export builds the hw-offset image)  */
        (const void*)SsDbgWramL(),   /* low work RAM  (host order; verify — §Byte order) */
        (const void*)SsDbgWramH(),   /* high work RAM (host order; verify)      */
        (const void*)SsDbgVdp1Fb(),  /* VDP1 framebuffer (displayed bank, RGB555) */
        msh2, ssh2);                 /* SH-2 master + slave                     */
#else
    (void)v1; (void)v2; (void)vs; (void)vdp1; (void)msh2; (void)ssh2;
    (void)SwapU16ToBE; (void)BuildYabauseVdp2Struct;
#endif
}

/* ============================ debugger bridge (Tier 3) ==================== */
/* Wire these to Mednafen's ss debug API, then register them once at init with
 * SeExportSetBreakpointHooks(SeMdfnAddExecBp, SeMdfnClearBps) and
 * SeExportSetMemWriteHook(SeMdfnWriteByte). Call SeExportNotifyStop(cpu, pc) from
 * Mednafen's breakpoint callback. */
static void SeMdfnAddExecBp(int cpu, unsigned int address)
{
    (void)cpu; (void)address;
    /* TODO(mednafen): install an execution breakpoint on CPU[cpu] at 'address'
       via the ss debugger API. */
}
static void SeMdfnClearBps(void)
{
    /* TODO(mednafen): remove all execution breakpoints on both SH-2 cores. */
}
static void SeMdfnWriteByte(unsigned int address, unsigned char value)
{
    (void)address; (void)value;
    /* TODO(mednafen): SsDbgPokeByte(address, value); — big-endian preserved by
       writing byte-by-byte at Saturn addresses, no manual swap. */
}

/* ============================ lifecycle wiring ============================ */
/* Wire these into Mednafen ss (all sites in ss.cpp unless noted):
 *   - end of Load(GameFile*):     SeExportInit();
 *                                 SeExportSetBreakpointHooks(SeMdfnAddExecBp, SeMdfnClearBps);
 *                                 SeExportSetMemWriteHook(SeMdfnWriteByte);
 *   - end-of-frame in Emulate():  SeMednafenSnapshot();   (after espec->MasterCycles = ...)
 *   - top of Emulate() (optional pause/step):  while (!SeExportGateFrame()) { }
 *       (Tier-1 read-only view can skip the gate; see README frame-gate caveat.)
 *   - CloseGame():                SeExportDeinit();
 */
void SeMednafenSuppressUnusedWarnings(void)
{
    (void)SeMdfnAddExecBp; (void)SeMdfnClearBps; (void)SeMdfnWriteByte;
}
