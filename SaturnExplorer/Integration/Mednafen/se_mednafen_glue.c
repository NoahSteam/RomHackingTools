/* Saturn Explorer — standalone Mednafen live-tap glue.  *** TEMPLATE ***
 *
 * Targets STANDARD Mednafen (the standalone emulator), NOT the Beetle Saturn
 * libretro core — they share the ss/ Saturn code but are different build targets
 * (this hooks ss.cpp's Emulate(); Beetle would hook retro_run). See DISTRIBUTION.md.
 *
 * This is the emulator-specific half of the live tap. The portable half is
 * ../Common/se_export.c (unchanged). Drop this + se_export.{c,h} + SeLiveProtocol.h
 * into Mednafen's ss core, then wire the calls at the bottom into ss.cpp's frame
 * loop. Symbol names were mapped against libretro-mirrors/mednafen-git src/ss — a git
 * mirror of standard Mednafen's source (Mednafen's own repo is Mercurial), NOT Beetle
 * (see
 * README.md); the injected ss accessors (apply.py) carry any remaining build-specific
 * TODO(mednafen) confirm points.
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
#include <stdio.h>

/* ---- Accessors the patcher injects into Mednafen ss (C linkage). Each reaches a
 *      file-scope static the glue can't see directly. Pointer accessors return the
 *      live array; the two packing accessors run Mednafen-side because they touch
 *      class/static members (VDP1's individual regs, SH7095's register file). ---- */
#if defined(SE_MEDNAFEN_WIRED)  /* define once the injected ss accessors exist (README §Accessors). */
extern const uint16_t* SsDbgVdp1Vram(void);   /* VDP1::VRAM    — 0x40000 words, host order */
extern const uint16_t* SsDbgVdp1Latch(void);  /* VDP1 VRAM latched at draw-end; NULL until 1st draw */
extern const uint16_t* SsDbgVdp2Vram(void);   /* VDP2::VRAM    — 262144 words, host order  */
extern const uint16_t* SsDbgCram(void);       /* VDP2::CRAM    — 2048 words,  host order   */
extern const uint16_t* SsDbgRawRegs(void);    /* VDP2::RawRegs — 0x100, indexed (hw>>1)    */
extern const uint16_t* SsDbgWramL(void);      /* WorkRAML      — 0x80000 words @ 0x00200000 */
extern const uint16_t* SsDbgWramH(void);      /* WorkRAMH      — 0x80000 words @ 0x06000000 */
extern const uint16_t* SsDbgVdp1Fb(void);     /* displayed VDP1 FB bank = FB[!FBDrawWhich]  */
extern const uint16_t* SsDbgSoundRam(void);   /* SCSP RAM — 262144 words, host order; NULL if unwired */
extern int             SsDbgScspSlots(unsigned char* out); /* fill SE_LIVE_SCSP_BLOCK_LEN bytes; return count (0 if unwired) */
extern int             SsDbgCdStatus(unsigned char* out); /* fill SE_LIVE_CD_BLOCK_LEN bytes; return 1 if available (0 if unwired) */
extern size_t          SsDbgSaveState(unsigned char* buf, size_t cap); /* full savestate -> buf; return bytes (or size when buf==NULL) */
extern int             SsDbgLoadState(const unsigned char* buf, size_t len); /* restore full savestate; 0 = ok */
extern int             SsDbgEmuSlotInfo(unsigned slot, unsigned long long* mtime); /* 1 if the emulator's own slot holds a state */
extern int             SsDbgEmuLoadSlot(unsigned slot); /* have the emulator load its own slot; 0 = ok */
extern void            SsDbgVdp1Regs(uint16_t out11[11]); /* TVMR,FBCR,PTMR,EWDR,EWLR,EWRR,ENDR,EDSR,LOPR,COPR,MODR */
extern void            SsDbgSh2Regs(int cpu, uint32_t out23[23]); /* R[16],SR,GBR,VBR,MACH,MACL,PR,PC */
extern void            SsDbgPokeByte(uint32_t addr, uint8_t val); /* bus/debug byte write */
extern void            SsDbgAddExecBp(int cpu, unsigned int addr); /* Tier 3: install PC breakpoint */
extern void            SsDbgAddMemBp(int cpu, unsigned int addr, unsigned int size, unsigned int kind); /* data watchpoint */
extern void            SsDbgClearBps(void);                        /* Tier 3: clear PC + data breakpoints */
extern void            SsDbgSetTraceActive(int active);           /* arm per-insn tracepoint scan */
/* Controller injection (v7+). apply.py implements this accessor through the SMPC
 * gamepad path, translating SE_PAD_* to Mednafen's bit order and atomically overlaying
 * it after each host-input refresh. `port` is 0-based (0 = controller 1). */
extern void            SsDbgSetPad(unsigned int port, unsigned int buttons);
extern unsigned short  SsDbgReadOpcode(unsigned int addr); /* v9: 16-bit instr @ Saturn addr */
/* Live host keyboard bindings (v10+). apply.py injects this into src/drivers/input.cpp,
 * where the PIDC[] binding cache is in scope. Fills out[13] with the USB-HID scancode the
 * user has mapped to each Saturn pad button (ascending SE_PAD_* order), -1 if unbound. */
extern int             SsDbgQueryKeyMap(unsigned int port, int out[13]);
/* Port device-type name (v12+). apply.py injects this into smpc.cpp (where the port map
 * and PossibleDevices[] are in scope). Returns a short human-readable controller name for
 * port 0/1 ("Digital Control Pad", "3D Control Pad", "Mouse", ...). */
extern const char*     SsDbgPortDeviceName(unsigned int port);
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

/* Hardware-offset -> Yabause `Vdp2` struct byte-offset map, indexed by (hw >> 1);
 * 0xFFFF = reserved slot. The SAME 144-entry table the client uses — kept verbatim in
 * sync with Drivers/Common/src/SaturnStateShared.cpp (the client's inverse reads it
 * back). Embedded here (not extern) so the copied glue links standalone in a foreign
 * tree. (Longer term, protocol v7 sends a pre-built hw-offset BE image and drops this
 * rebuild entirely — see README "VDP2 registers".) */
static const unsigned short kSeVdp2RegStructOffset[144] = {
    0x000, 0x002, 0x004, 0x006, 0x008, 0x00A, 0xFFFF, 0x00C,
    0x00E, 0x010, 0x012, 0x014, 0x016, 0x018, 0x01A, 0x01C,
    0x01E, 0x020, 0x022, 0x024, 0x026, 0x028, 0x02A, 0x02C,
    0x02E, 0x030, 0x032, 0x034, 0x036, 0x038, 0x03A, 0x03C,
    0x03E, 0x040, 0x042, 0x044, 0x046, 0x048, 0x04A, 0x04C,
    0x04E, 0x050, 0x052, 0x054, 0x056, 0x058, 0x05A, 0x05C,
    0x05E, 0x060, 0x062, 0x064, 0x066, 0x068, 0x06A, 0x06C,
    0x06E, 0x070, 0x072, 0x074, 0x07A, 0x078, 0x07E, 0x07C,
    0x080, 0x082, 0x084, 0x086, 0x08A, 0x088, 0x08E, 0x08C,
    0x090, 0x092, 0x094, 0x096, 0x098, 0x09A, 0x09E, 0x09C,
    0x0A2, 0x0A0, 0x0A6, 0x0A4, 0x0AA, 0x0A8, 0x0AC, 0x0AE,
    0x0B0, 0x0B2, 0x0B4, 0x0B6, 0x0B8, 0x0BA, 0x0BE, 0x0BC,
    0x0C0, 0x0C2, 0x0C4, 0x0C6, 0x0C8, 0x0CA, 0x0CC, 0x0CE,
    0x0D0, 0x0D2, 0x0D4, 0x0D6, 0x0DA, 0x0D8, 0x0DE, 0x0DC,
    0x0E0, 0x0E2, 0x0E4, 0x0E6, 0x0E8, 0x0EA, 0x0EC, 0x0EE,
    0x0F0, 0x0F2, 0x0F4, 0x0F6, 0x0F8, 0x0FA, 0x0FC, 0xFFFF,
    0x0FE, 0x100, 0x102, 0x104, 0x106, 0x108, 0x10A, 0x10C,
    0x10E, 0x110, 0x112, 0x114, 0x116, 0x118, 0x11A, 0x11C,
};

/* Rebuild the raw 288-byte Yabause `Vdp2` struct the client expects, from
 * Mednafen's flat RawRegs[0x100] (register at hw offset o is RawRegs[o>>1]). The
 * struct is a re-layout of the same registers via the fixed map above; write each
 * register little-endian at its struct offset so the client's BuildVdp2RegImage reads
 * it back correctly. */
static void BuildYabauseVdp2Struct(uint8_t out288[288], const uint16_t rawRegs[0x100])
{
    unsigned hw;
    memset(out288, 0, 288);
    for (hw = 0; hw <= 0x11E; hw += 2)
    {
        const uint16_t so = kSeVdp2RegStructOffset[hw >> 1];
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
    static uint8_t  sr[SE_LIVE_SOUND_RAM_LEN];
    static uint8_t  sl[SE_LIVE_SCSP_BLOCK_LEN];
    static uint8_t  cd[SE_LIVE_CD_BLOCK_LEN];
    static uint16_t vdp1[11];
    static uint32_t msh2[23], ssh2[23];

#if defined(SE_MEDNAFEN_WIRED)  /* enabled once the injected accessors exist. */
    /* Nothing attached: return before the byte swaps below. SeExportSnapshot checks this too,
     * but by then this function has already swapped 1.5 MiB of VRAM into the scratch buffers,
     * which is most of the cost of preparing a frame. */
    /* Even with nobody attached: the breakpoints of a client that just left are dropped on this
     * thread, here, and they must go before one of them halts a game nobody can resume. */
    SeExportApplyInstalls();
    if (!SeExportHasClient()) return;
    /* Prefer the draw-end latch (the command table as it was actually plotted) over live
     * VRAM, which at this video-frame boundary may already be a half-rebuilt next-frame
     * table. Falls back to live VRAM until the first draw completes. */
    {
        const uint16_t* v1src = SsDbgVdp1Latch();
        if (!v1src) v1src = SsDbgVdp1Vram();
        SwapU16ToBE(v1, (const uint8_t*)v1src, sizeof v1);         /* -> big-endian */
    }
    SwapU16ToBE(v2, (const uint8_t*)SsDbgVdp2Vram(), sizeof v2);
    BuildYabauseVdp2Struct(vs, SsDbgRawRegs());
    SsDbgVdp1Regs(vdp1);
    SsDbgSh2Regs(0, msh2);
    SsDbgSh2Regs(1, ssh2);
    /* SCSP sound RAM. Beetle-Saturn holds it as uint16 host words (like VRAM), so swap to
     * Saturn big-endian bytes for the wire. If SsDbgSoundRam() is a NULL/undefined build,
     * ship an empty block (the client then just shows an empty Sound RAM tab). */
    {
        const void* srcSr = SsDbgSoundRam();
        if (srcSr) SwapU16ToBE(sr, (const uint8_t*)srcSr, sizeof sr);
    }
    /* Decoded SCSP voices: the injected accessor serializes z->Slots[32] into the wire block
     * (host-order fields, no swap). 0 = unwired -> ship no slot block. */
    const int scspSlotCount = SsDbgScspSlots(sl);
    /* Live CD-block status: the injected accessor fills the 16-byte wire record from the
     * CDB drive state (current/play FAD + status). 0 = unwired -> ship no CD block. */
    const int cdOk = SsDbgCdStatus(cd);

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
        msh2, ssh2,                  /* SH-2 master + slave                     */
        SsDbgSoundRam() ? (const void*)sr : (const void*)0,  /* SCSP sound RAM (v13) */
        scspSlotCount ? (const void*)sl : (const void*)0,    /* SCSP slots (v14)    */
        cdOk ? (const void*)cd : (const void*)0);            /* CD status (v15)     */
#else
    (void)v1; (void)v2; (void)vs; (void)sr; (void)sl; (void)cd; (void)vdp1; (void)msh2; (void)ssh2;
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
#if defined(SE_MEDNAFEN_WIRED)
    /* Installs a PC breakpoint via the ss debugger (SsDbgAddExecBp). It fires only in
     * a Mednafen built with --enable-debugger (WANT_DEBUGGER); otherwise it's a no-op
     * and the breakpoint still round-trips over the protocol without halting. */
    SsDbgAddExecBp(cpu, address);
#else
    (void)cpu; (void)address;
#endif
}
static void SeMdfnAddMemBp(int cpu, unsigned int address, unsigned int size, unsigned int kind)
{
    /* Diagnostic: confirm the emulator received the watchpoint (and the exact address/size/
     * access it installed) in Saturn Explorer's Log window. Fires on each breakpoint-set
     * sync, so a Log line here but no halt points at an address-match issue, while no line
     * at all points at a stale (non-mem-bp) build. */
    char msg[80];
    snprintf(msg, sizeof(msg), "watchpoint: addr=%08X size=%u %s%s", address, size,
             (kind & 0x1u) ? "R" : "", (kind & 0x2u) ? "W" : "");
    SeExportLog(msg);
#if defined(SE_MEDNAFEN_WIRED)
    /* Installs a data (read/write) watchpoint over [address, address+size) via the ss
     * debugger. Like the PC breakpoint it only halts in a --enable-debugger build;
     * otherwise it round-trips without halting. */
    SsDbgAddMemBp(cpu, address, size, kind);
#else
    (void)cpu; (void)address; (void)size; (void)kind;
#endif
}
static void SeMdfnClearBps(void)
{
#if defined(SE_MEDNAFEN_WIRED)
    SsDbgClearBps();
#endif
}
static void SeMdfnWriteByte(unsigned int address, unsigned char value)
{
#if defined(SE_MEDNAFEN_WIRED)
    SsDbgPokeByte(address, value);   /* cache-correct bus poke (see apply.py accessor) */
#else
    (void)address; (void)value;
#endif
}
/* Sound-RAM poke (v13): the offset is 0-based within the 512 KiB SCSP RAM. Route it through
 * the same bus writer at the sound-RAM base (0x25A00000), so no SCSP-internal write symbol is
 * needed and cache/bus semantics stay correct. */
static void SeMdfnWriteSoundByte(unsigned int offset, unsigned char value)
{
#if defined(SE_MEDNAFEN_WIRED)
    SsDbgPokeByte(0x25A00000u + offset, value);
#else
    (void)offset; (void)value;
#endif
}

/* Full-savestate save/load (v16 rewind). save() writes the current state into buf (or returns
 * the required size when buf==NULL); load() restores it. se_export's worker delta-compresses the
 * saved states off the emulate thread; a NULL/stub save hook simply leaves the ring empty and
 * the rewind feature disabled. Wired to Mednafen's MDFNSS_SaveSM/LoadSM via apply.py. */
static size_t SeMdfnSaveState(unsigned char* buf, size_t cap)
{
#if defined(SE_MEDNAFEN_WIRED)
    return SsDbgSaveState(buf, cap);
#else
    (void)buf; (void)cap; return 0;   /* stub: no ring -> rewind unavailable */
#endif
}
static int SeMdfnLoadState(const unsigned char* buf, size_t len)
{
#if defined(SE_MEDNAFEN_WIRED)
    return SsDbgLoadState(buf, len);
#else
    (void)buf; (void)len; return -1;
#endif
}

/* Controller input (v7+): drive the emulated pad directly from the SE_PAD_* mask so
 * the Saturn Explorer controller panel controls the game, bypassing Mednafen's own
 * host-input mapping. The injected SsDbgSetPad accessor does the emulator-specific
 * map + latch (see its declaration above). Registered with SeExportSetInputHook. */
static void SeMdfnSetPad(unsigned int port, unsigned int buttons)
{
    /* Diagnostic: log the pad state the glue receives so Saturn Explorer's Log window
     * shows what actually reached the emulator. Only on change: the LiveDriver poll
     * thread re-sends INP every cycle while a button is held (to cover a non-latching
     * glue), so logging every receipt would flood the Log at poll rate. */
    static unsigned int seLastRecv[2] = { 0xFFFFFFFFu, 0xFFFFFFFFu };
    if (port < 2 && (buttons & 0x1FFFu) != seLastRecv[port])
    {
        char msg[80];
        snprintf(msg, sizeof(msg), "glue recv: port=%u buttons=0x%04X", port, buttons & 0x1FFFu);
        SeExportLog(msg);
        seLastRecv[port] = buttons & 0x1FFFu;
    }
#if defined(SE_MEDNAFEN_WIRED)
    SsDbgSetPad(port, buttons);
#else
    (void)port; (void)buttons;
#endif
}

/* Live host keyboard bindings (v10+): report the scancode the user has mapped to each
 * Saturn pad button so the Saturn Explorer controller panel can mirror it automatically.
 * The injected SsDbgQueryKeyMap reads Mednafen's own PIDC[] binding cache (see its
 * declaration above). Registered with SeExportSetKeyMapHook. */
static int SeMdfnGetKeyMap(unsigned int port, int out[13])
{
#if defined(SE_MEDNAFEN_WIRED)
    return SsDbgQueryKeyMap(port, out);
#else
    int i;
    for (i = 0; i < 13; i++) out[i] = -1;
    (void)port;
    return 0;
#endif
}

/* Port device type (v12+): report which controller each port is configured as, so the
 * Saturn Explorer Log window shows the emulator's input setup when a client connects.
 * The injected SsDbgPortDeviceName reads Mednafen's own port map (see its declaration
 * above). Registered with SeExportSetPortInfoHook. */
static const char* SeMdfnPortDeviceName(unsigned int port)
{
#if defined(SE_MEDNAFEN_WIRED)
    return SsDbgPortDeviceName(port);
#else
    (void)port;
    return "?";
#endif
}

/* ============================ tracepoints (Tier 4, v8+) =================== */
/* Tracepoints are non-halting: on a hit the glue captures the SH-2 register file and
 * queues an event (the client formats the message), then execution CONTINUES. The set
 * arrives via SeMdfnSetTracepoints (registered with SeExportSetTracepointHook); the
 * per-instruction check happens in SeMednafenTraceHook, which the injected SS debugger
 * callback (SeSsBpHook, via SsDbgSetTraceActive) calls every instruction while any
 * tracepoint is armed (see README "Tracepoints"; needs --enable-debugger, exactly like
 * execution breakpoints). */
#define SE_MDFN_TP_MAX 64
/* The installed set, with each tracepoint's repeat state (v21): the executions seen and whether a
 * fire-once one has spent itself. EMULATE-THREAD ONLY. SeMdfnSetTracepoints is run by the exporter on
 * the emulate thread (it publishes a set into a mailbox and the emulate thread takes it at its next
 * gate or frame), and the per-instruction scan is on the same thread, so nothing here is shared and
 * nothing needs a lock -- the scan can never see a set that is half replaced. A tracepoint that comes
 * through a re-install unchanged (same id, CPU, address and flags) keeps its counts: editing the log
 * format of one tracepoint, or a fire-once one elsewhere spending itself, must not restart the
 * "every 1000th" of another. */
typedef struct { unsigned int id, cpu, address, flags, hits; unsigned char spent; } SeMdfnTpState;
static SeMdfnTpState sTpState[SE_MDFN_TP_MAX];
static unsigned int sTpStateCount;
static unsigned int SeRd32LE(const unsigned char* p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) |
           ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

/* SeExportSetTracepointHook target: replace the working set. Runs on the emulate thread. */
static void SeMdfnSetTracepoints(unsigned int count, const unsigned char* descs)
{
    SeMdfnTpState next[SE_MDFN_TP_MAX];
    unsigned int i, j;
    if (count > SE_MDFN_TP_MAX) count = SE_MDFN_TP_MAX;
    for (i = 0; i < count; ++i)
    {
        const unsigned char* d = descs + i * SE_LIVE_TRACE_DESC_LEN;
        next[i].id      = SeRd32LE(d);
        next[i].cpu     = SeRd32LE(d + 4);
        next[i].address = SeRd32LE(d + 8);
        next[i].flags   = SeRd32LE(d + 12);
        next[i].hits    = 0;
        next[i].spent   = 0;
        for (j = 0; j < sTpStateCount; ++j)
        {
            if (sTpState[j].id == next[i].id && sTpState[j].cpu == next[i].cpu &&
                sTpState[j].address == next[i].address && sTpState[j].flags == next[i].flags)
            {
                next[i].hits  = sTpState[j].hits;
                next[i].spent = sTpState[j].spent;
                break;
            }
        }
    }
    for (i = 0; i < count; ++i) sTpState[i] = next[i];
    sTpStateCount = count;
#if defined(SE_MEDNAFEN_WIRED)
    /* Arm/disarm the per-instruction SS debugger callback: it only needs to run every
     * instruction while at least one tracepoint is enabled. Without this the trace hook
     * is never called and tracepoints never fire (the SS fast run loop skips hooks). */
    {
        int anyEnabled = 0;
        for (i = 0; i < count; ++i)
            if (sTpState[i].flags & SE_LIVE_TP_ENABLED) { anyEnabled = 1; break; }
        SsDbgSetTraceActive(anyEnabled);
    }
#endif
}

/* Whether this execution of tracepoint 'i' fires, under the repeat policy the client installed it
 * with (SE_LIVE_TP_* in SeLiveProtocol.h). Applied where the instruction runs, so a tracepoint on
 * hot code with "every 1000th" does not queue a thousand events to be thrown away.
 *   GUARDED: the client has a condition we cannot evaluate; forward every execution and let it
 *            count the ones whose condition held.
 *   ONCE:    fire on the first execution, then never again until the client sends it enabled again
 *            (which changes its flags, and so restarts it).
 *   every-N: fire on the Nth, 2Nth, ... execution.
 * 'hits' counts executions seen, whether or not they fire. */
static int SeMdfnTpFires(unsigned int i)
{
    SeMdfnTpState* t = &sTpState[i];
    unsigned int every;
    if (!(t->flags & SE_LIVE_TP_ENABLED)) return 0;
    if (t->flags & SE_LIVE_TP_GUARDED) return 1;
    if (t->spent) return 0;
    ++t->hits;
    if (t->flags & SE_LIVE_TP_ONCE)
    {
        t->spent = 1;
        return 1;
    }
    every = t->flags >> SE_LIVE_TP_EVERY_SHIFT;
    if (every > 1u) return (t->hits % every) == 0u;
    return 1;
}

/* ---- Shadow call stack (v9) --------------------------------------------------------
 * Mirror the SH-2's own control flow into se_export's per-CPU stack:
 *   bsr  disp   1011 dddd dddd dddd   call, target = PC + 4 + sign12(disp)*2
 *   bsrf Rn     0000 nnnn 0000 0011   call, target = PC + 4 + Rn
 *   jsr  @Rn    0100 nnnn 0000 1011   call, target = Rn
 *   rts         0000 0000 0000 1011   return from a call
 *   trapa #imm  1100 0011 iiii iiii   exception ENTRY, handler = @(VBR + imm*4)
 *   rte         0000 0000 0010 1011   return from an exception
 * SH-2 calls place the return one instruction past the delay slot (PC+4); trapa has no
 * delay slot, so its return is PC+2. Targets we can't resolve are pushed as 0. trapa is
 * the one exception entry a per-instruction hook can see; asynchronous interrupts are
 * invisible from here. se_export.h has the why — in particular why an rte must not be
 * allowed to unwind a call frame.
 *
 * Deliberately not tracked: `jmp @Rn` — a tail call after restoring PR — pushes nothing,
 * and the callee's rts unwinds the original caller's frame, which is the right depth;
 * `braf Rn`, `bra`, `bt`, `bf` and friends never create or destroy a frame. */
typedef enum
{
    SeFlowNone = 0,
    SeFlowCallDisp,     /* bsr  disp  */
    SeFlowCallPcRelReg, /* bsrf Rn    */
    SeFlowCallReg,      /* jsr  @Rn   */
    SeFlowReturn,       /* rts        */
    SeFlowTrap,         /* trapa #imm */
    SeFlowExcReturn     /* rte        */
} SeFlowKind;

static SeFlowKind SeMdfnClassifyFlow(unsigned short op)
{
    if ((op & 0xF000u) == 0xB000u) return SeFlowCallDisp;
    if ((op & 0xF0FFu) == 0x0003u) return SeFlowCallPcRelReg;
    if ((op & 0xF0FFu) == 0x400Bu) return SeFlowCallReg;
    if ((op & 0xFF00u) == 0xC300u) return SeFlowTrap;
    if (op == 0x000Bu)             return SeFlowReturn;
    if (op == 0x002Bu)             return SeFlowExcReturn;
    return SeFlowNone;
}

/* Apply one instruction's effect on the shadow stack. Split from SeMdfnTrackFlow — which
 * reads the opcode and the register file through the injected accessors — so the model
 * can be driven from a host test with a synthetic instruction stream (see
 * Integration/tests/ShadowCallStackTests.c). 'op' still carries bsr's displacement;
 * 'rn' is the source register for bsrf/jsr, 'sp' is R15 as the instruction sees it,
 * 'handler' the trapa vector's target. */
static void SeMdfnApplyFlow(int cpu, SeFlowKind kind, unsigned int pc, unsigned short op,
                            unsigned int rn, unsigned int sp, unsigned int handler)
{
    const unsigned int ret = pc + 4;   /* past the delay slot: where a call comes back to */
    switch (kind)
    {
        case SeFlowCallDisp:
        {
            int disp = (int)(op & 0x0FFFu);
            if (disp & 0x0800) disp -= 0x1000;         /* sign-extend 12-bit */
            SeExportPushFrame(cpu, pc, pc + 4 + (unsigned int)(disp << 1), ret, sp, 0);
            break;
        }
        case SeFlowCallPcRelReg:
            SeExportPushFrame(cpu, pc, pc + 4 + rn, ret, sp, 0);
            break;
        case SeFlowCallReg:
            SeExportPushFrame(cpu, pc, rn, ret, sp, 0);
            break;
        case SeFlowReturn:
            SeExportPopFrame(cpu);
            break;
        case SeFlowTrap:
            /* The entry pushes SR then PC+2, so the handler — and the rte that ends it —
             * run with R15 - 8. Record that as the frame's SP so the rte can be matched
             * to this frame and to no other. */
            SeExportPushExceptionFrame(cpu, pc, handler, pc + 2, sp - 8, 0);
            break;
        case SeFlowExcReturn:
            SeExportPopExceptionFrame(cpu, sp);
            break;
        case SeFlowNone:
            break;
    }
}

/* Control flow takes effect when it COMPLETES, not when it is first seen. The per-instruction hook
 * runs BEFORE the instruction does, and on the SH-2 a call, `rts` and `rte` have a delay slot: the
 * transfer happens after the slot has run. Applying a flow instruction at its own hook therefore
 * drew the wrong stack for as long as the instruction was in flight -- a breakpoint on B's `rts`
 * found B already gone from the recorded stack although it was still executing (Step Out then
 * targeted B's caller's return), and a breakpoint on a `bsr` found its callee already pushed.
 *
 * So a flow instruction is held pending, per CPU, and applied at the first hook AFTER its slot:
 *   stage 1  seen; waiting for the delay slot (a repeat of the flow instruction's own PC, which a
 *            bus-stalled CPU presents again, changes nothing)
 *   stage 2  the slot is about to run (its repeats are ignored too); the next other PC is the
 *            transfer's target, and the flow is applied there -- before that hook's own halt, so a
 *            breakpoint at the target sees the completed stack
 * `trapa` has no delay slot, so it goes straight to stage 2. A PC that is neither the slot nor a repeat
 * is where the transfer landed after something invisible ran in between (an interrupt is taken before
 * the target executes), so it completes there. That only holds while every instruction is presented:
 * see SeMednafenHookMode for a run that is not watched. The state is dropped when the call stacks are
 * reset (a savestate load replaced the timeline it belonged to). */
typedef struct
{
    int          stage;          /* 0 nothing pending, 1 waiting for the slot, 2 waiting for the target */
    unsigned int flowPc;         /* the flow instruction */
    unsigned int waitPc;         /* stage 1: the slot; stage 2: the PC whose repeats are ignored */
    SeFlowKind   kind;
    unsigned short op;
    unsigned int rn, sp, handler;
    unsigned int epoch;          /* SeExportCallStackEpoch when it was seen */
} SeMdfnFlowPending;
static SeMdfnFlowPending sFlowPending[2];

/* One hook presentation of 'pc' on 'cpu': advance whatever is pending. Returns 1 when this
 * presentation is part of a pending flow instruction (its own repeat, or its delay slot), so the
 * caller must not treat it as a new instruction to classify. */
static int SeMdfnFlowAdvance(int cpu, unsigned int pc)
{
    SeMdfnFlowPending* p = &sFlowPending[cpu ? 1 : 0];
    if (p->stage == 0) return 0;
    if (p->epoch != SeExportCallStackEpoch())
    {
        p->stage = 0;   /* the stacks were reset under it: it belongs to a timeline that is gone */
        return 0;
    }
    if (p->stage == 1)
    {
        if (pc == p->flowPc) return 1;                          /* the instruction again (bus stall) */
        if (pc == p->waitPc) { p->stage = 2; return 1; }        /* its delay slot, about to run */
    }
    else if (pc == p->waitPc)
    {
        return 1;                                               /* the slot again (bus stall) */
    }
    /* The transfer has completed: this PC is where it landed (or a later one, if the hook missed it). */
    SeMdfnApplyFlow(cpu, p->kind, p->flowPc, p->op, p->rn, p->sp, p->handler);
    p->stage = 0;
    return 0;
}

/* The emulator tells the glue what the per-instruction callback does from here on: 'continuous' when
 * every instruction will be presented (a tracepoint is armed or an instruction step is running), else
 * it is called only at a breakpoint's PC. A held flow instruction is completed by the presentations
 * that FOLLOW it, so when those stop coming its completion can no longer be observed, and applying it
 * at whatever breakpoint is reached next draws the wrong stack -- a call stepped over has long since
 * returned by then, and would be recorded as still running.
 *   a call or trap  entered and left again, or still running, with nothing to say which: dropped, and
 *                   the stack stays as it was. A breakpoint inside the callee finds no frame for it,
 *                   which is the cost of not watching it run.
 *   rts / rte       certain to complete once the CPU runs on, so it is applied now: otherwise the frame
 *                   it ends stays on the stack for good.
 * Called whenever the callback is (re)selected for a CPU that is about to run; not for a change made
 * while it is halted, which is no change at all. */
void SeMednafenHookMode(int continuous)
{
    int c;
    if (continuous) return;
    for (c = 0; c < 2; ++c)
    {
        SeMdfnFlowPending* p = &sFlowPending[c];
        if (p->stage != 0 && p->epoch == SeExportCallStackEpoch() &&
            (p->kind == SeFlowReturn || p->kind == SeFlowExcReturn))
            SeMdfnApplyFlow(c, p->kind, p->flowPc, p->op, p->rn, p->sp, p->handler);
        p->stage = 0;
    }
}

/* A flow instruction at 'pc' has been presented: hold it until it completes. */
static void SeMdfnFlowDefer(int cpu, unsigned int pc, SeFlowKind kind, unsigned short op,
                            unsigned int rn, unsigned int sp, unsigned int handler)
{
    SeMdfnFlowPending* p = &sFlowPending[cpu ? 1 : 0];
    p->kind = kind; p->op = op; p->rn = rn; p->sp = sp; p->handler = handler;
    p->flowPc = pc;
    p->epoch = SeExportCallStackEpoch();
    if (kind == SeFlowTrap) { p->stage = 2; p->waitPc = pc; }   /* no delay slot */
    else                    { p->stage = 1; p->waitPc = pc + 2; }
}

#if defined(SE_MEDNAFEN_WIRED)
/* Read what the instruction at PC needs and hold it as pending flow. The opcode read is
 * per-instruction; the register-file read is not -- only the handful of opcodes that move
 * the stack need it, and rts needs nothing at all. */
static void SeMdfnTrackFlow(int cpu, unsigned int pc)
{
    unsigned short op;
    SeFlowKind kind;
    unsigned int rn = 0, sp = 0, handler = 0;
    if (SeMdfnFlowAdvance(cpu, pc)) return;   /* a pending flow instruction's repeat or delay slot */
    op = SsDbgReadOpcode(pc);
    kind = SeMdfnClassifyFlow(op);
    if (kind == SeFlowNone) return;     /* the overwhelmingly common case */
    if (kind != SeFlowReturn)           /* rts needs no register read; everything else does */
    {
        unsigned int raw[23];
        SsDbgSh2Regs(cpu, raw);
        sp = raw[15];
        rn = raw[(op >> 8) & 0xF];   /* the bsrf/jsr source; harmless for the rest */
        if (kind == SeFlowTrap)
        {
            /* TRAPA's handler is the longword at VBR + imm*4. SsDbgReadOpcode is a plain
             * 16-bit big-endian debug read, so take the vector as two halves. raw[18] is
             * VBR (SsDbgSh2Regs order: R[16], SR, GBR, VBR, MACH, MACL, PR, PC). */
            const unsigned int vec = raw[18] + ((unsigned int)(op & 0x00FFu) << 2);
            handler = ((unsigned int)SsDbgReadOpcode(vec) << 16) | SsDbgReadOpcode(vec + 2);
        }
    }
    SeMdfnFlowDefer(cpu, pc, kind, op, rn, sp, handler);
}
#endif

/* Is 'op' (the instruction at 'pc') a branch that, with status register 'sr', will be TAKEN back to
 * 'pc' itself? Only the non-delayed conditional branches can do this and still retire with the PC
 * unchanged -- a delayed branch runs its delay slot first, which moves the PC:
 *   bt  disp   1000 1001 dddd dddd   branch if T=1, target = pc + 4 + sign8(disp)*2
 *   bf  disp   1000 1011 dddd dddd   branch if T=0
 * so disp = -2 (0xFE) lands on the branch. The instruction-step counter needs this because a
 * repeated PC is otherwise indistinguishable from a bus-stalled instruction that has not run. */
static int SeMdfnIsTakenSelfBranch(unsigned short op, unsigned int sr)
{
    if ((op & 0x00FFu) != 0x00FEu) return 0;
    if ((op & 0xFF00u) == 0x8900u) return (sr & 1u) != 0;   /* bt: taken when T is set   */
    if ((op & 0xFF00u) == 0x8B00u) return (sr & 1u) == 0;   /* bf: taken when T is clear */
    return 0;
}

/* The same question for the instruction about to run on 'cpu' at 'pc' (the per-instruction hook
 * asks it only while an instruction step is in progress, so the opcode read is not per-instruction
 * in general). */
int SeMednafenSelfBranchTaken(int cpu, unsigned int pc)
{
#if defined(SE_MEDNAFEN_WIRED)
    const unsigned short op = SsDbgReadOpcode(pc);
    unsigned int raw[23];
    if ((op & 0x00FFu) != 0x00FEu || ((op & 0xFF00u) != 0x8900u && (op & 0xFF00u) != 0x8B00u))
        return 0;   /* the common case: nothing else to read */
    SsDbgSh2Regs(cpu, raw);
    return SeMdfnIsTakenSelfBranch(op, raw[16]);   /* raw[16] = SR */
#else
    (void)cpu; (void)pc;
    return 0;
#endif
}

/* Per-instruction hook (apply.py injects a call: SeMednafenTraceHook(cpu, PC) from the
 * SS CPU dispatch / DBG_CPUHook). Does two per-instruction jobs: (1) if PC matches an
 * enabled tracepoint on this CPU, capture the registers and queue an event (no halt);
 * (2) mirror calls/returns into the v9 shadow call stack. SsDbgSh2Regs returns
 * R0..R15,SR,GBR,VBR,MACH,MACL,PR,PC; reorder into se_sh2_regs order
 * (r[0..15],pc,pr,sr,gbr,vbr,mach,macl) which is what the event carries. */
void SeMednafenTraceHook(int cpu, unsigned int pc)
{
#if defined(SE_MEDNAFEN_WIRED)
    unsigned int i;
    for (i = 0; i < sTpStateCount; ++i)
    {
        if ((sTpState[i].flags & SE_LIVE_TP_ENABLED) && (int)sTpState[i].cpu == cpu &&
            sTpState[i].address == pc && SeMdfnTpFires(i))
        {
            unsigned int raw[23], regs[23];
            int k;
            SsDbgSh2Regs(cpu, raw);
            for (k = 0; k < 16; ++k) regs[k] = raw[k];   /* r0..r15 */
            regs[16] = raw[22];  /* pc   */
            regs[17] = raw[21];  /* pr   */
            regs[18] = raw[16];  /* sr   */
            regs[19] = raw[17];  /* gbr  */
            regs[20] = raw[18];  /* vbr  */
            regs[21] = raw[19];  /* mach */
            regs[22] = raw[20];  /* macl */
            SeExportQueueTraceEvent(sTpState[i].id, (unsigned int)cpu, regs);
        }
    }
    SeMdfnTrackFlow(cpu, pc);   /* v9 shadow call stack */
#else
    (void)cpu; (void)pc;
#endif
}

/* ============================ lifecycle wiring ============================ */
/* The patcher (apply.py) injects exactly ONE call — SeMednafenFrameHook() — at the
 * end-of-frame anchor in ss.cpp's Emulate() (after `espec->MasterCycles = ...`). That
 * hook lazy-starts the server on the first frame (registering the write + breakpoint
 * hooks), so no separate Load()/CloseGame() anchoring is needed. The optional
 * pause/step gate is the only extra site — `while (!SeExportGateFrame()) { }` at the
 * top of Emulate(); Tier-1 read-only view can skip it (see README frame-gate caveat).
 * The process teardown reclaims the server thread; call SeExportDeinit() from
 * CloseGame() too if you want a clean per-game stop. */
#if defined(SE_MEDNAFEN_WIRED)
/* The second injected call, at the very end of Emulate(): the frame is over, its timestamps are
 * rebased, and the machine is in exactly the state the next frame will start from. */
void SeMednafenEndFrameHook(void)
{
    SeExportEndFrame();
}

void SeMednafenFrameHook(void)
{
    static int inited = 0;
    if (!inited)
    {
        inited = 1;
        SeExportInit();
        SeExportSetMemWriteHook(SeMdfnWriteByte);
        SeExportSetSoundWriteHook(SeMdfnWriteSoundByte);   /* Sound RAM pokes (v13) */
        SeExportSetSaveStateHook(SeMdfnSaveState);         /* rewind savestate ring (v16) */
        SeExportSetLoadStateHook(SeMdfnLoadState);
        SeExportSetEmuSlotHooks(SsDbgEmuSlotInfo, SsDbgEmuLoadSlot);  /* Mednafen's own slots (v17) */
        SeExportSetBreakpointHooks(SeMdfnAddExecBp, SeMdfnClearBps);
        SeExportSetMemBreakpointHook(SeMdfnAddMemBp);   /* data (read/write) watchpoints */
        SeExportSetInputHook(SeMdfnSetPad);   /* controller panel -> emulated pad (v7+) */
        SeExportSetKeyMapHook(SeMdfnGetKeyMap);   /* emulator keyboard bindings -> panel (v10+) */
        SeExportSetPortInfoHook(SeMdfnPortDeviceName);  /* controller config -> Log on connect (v12+) */
        SeExportSetTracepointHook(SeMdfnSetTracepoints);  /* tracepoints (v8+) */
    }
    SeMednafenSnapshot();
}
#else
/* Stub build: these helpers are referenced only by SeMednafenFrameHook / the injected
 * per-insn call, neither compiled here — keep the compiler quiet without them.
 * SeMednafenTraceHook is public (apply.py injects a call to it) so it's compiled either
 * way; SeMdfnSetTracepoints/SeRd32LE are only used under SE_MEDNAFEN_WIRED. */
void SeMednafenEndFrameHook(void) {}   /* stub build: no savestates to take */
void SeMednafenSuppressUnusedWarnings(void)
{
    (void)SeMdfnAddExecBp; (void)SeMdfnAddMemBp; (void)SeMdfnClearBps; (void)SeMdfnWriteByte;
    (void)SeMdfnWriteSoundByte; (void)SeMdfnSetPad;
    (void)SeMdfnGetKeyMap; (void)SeMdfnPortDeviceName;
    (void)SeMdfnSetTracepoints; (void)SeRd32LE;
    (void)SeMdfnSaveState; (void)SeMdfnLoadState;
    (void)SeMdfnApplyFlow;   /* reached only through the wired SeMdfnTrackFlow */
}
#endif
