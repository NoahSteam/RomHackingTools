#!/usr/bin/env python3
"""
apply.py — wire the Saturn Explorer live-tap into a standalone Mednafen tree.

This targets STANDARD Mednafen (the standalone emulator: `Emulate()` in ss.cpp,
autotools `./configure --enable-debugger && make`) — NOT the Beetle Saturn libretro
core, which wraps the same `ss/` code behind `retro_run` and a different build. See
DISTRIBUTION.md ("Mednafen vs. Beetle Saturn").

Mednafen keeps the memory Saturn Explorer wants as *file-scope static* (VDP2
VRAM/CRAM/RawRegs, WorkRAML/H, the VDP1 registers), unreachable by `extern` from a
separate file. So this patcher injects tiny C-linkage **accessors** at the end of
vdp1.cpp / vdp2.cpp / ss.cpp / sound.cpp (where those statics are visible — a re-opened
namespace block; the SCSP sound-RAM + decoded-voice reads live in sound.cpp, backed by a
public SeDbgReadSlots member added to scsp.h), copies the portable server
(../Common/se_export.{c,h} + SeLiveProtocol.h) plus
the Mednafen glue (se_mednafen_glue.c) into the tree, and injects ONE per-frame call
into ss.cpp's Emulate(). That call lazy-starts the server on the first frame, so no
Load()/CloseGame() anchoring is needed.

Usage:
    python3 apply.py /path/to/mednafen                 # apply (or re-apply)
    python3 apply.py /path/to/mednafen --check          # report status, change nothing
    python3 apply.py /path/to/mednafen --revert         # remove edits + copied files
    python3 apply.py /path/to/mednafen --with-pause     # also inject the pause/step gate

It is idempotent: accessor/hook blocks are fenced with `SE_EXPORT` markers (updated in
place, never duplicated) and --revert removes them. If an anchor can't be found (a
Mednafen version moved the code), the script says exactly what to add by hand — see
README.md. Symbol names were mapped against libretro-mirrors/mednafen-git src/ss.

Everything wires up: live view, Watch/Hex (incl. work-RAM pokes), SH-2 registers,
pause/step, and **Tier-3 execution breakpoints** (via Mednafen's ss/debug.inc — these
fire only in a `--enable-debugger` build; a normal build compiles the no-op stubs and
the breakpoint set round-trips without halting).

Target layout assumed: <mednafen>/src/ss/{vdp1.cpp,vdp2.cpp,ss.cpp}. Pass the repo
root; the script also accepts the src/ss dir directly.
"""

import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SHARED_DIR = os.path.normpath(os.path.join(HERE, "..", "Common"))
# Copied verbatim from Common; the glue is copied from HERE with SE_MEDNAFEN_WIRED
# prepended so no per-file build flag is needed.
COMMON_FILES = ["se_export.c", "se_export.h", "SeLiveProtocol.h", "SeStateCodec.h"]
GLUE_FILE = "se_mednafen_glue.c"

BEGIN = "/* --- SE_EXPORT (Saturn Explorer live tap) --- */"
END = "/* --- end SE_EXPORT --- */"


def fence(code):
    return f"{BEGIN}\n{code}\n{END}"


# ---- Accessor blocks appended at end-of-file (file-scope statics are visible to a
#      re-opened namespace block later in the same translation unit). Keyed by a
#      stable accessor name so re-runs update in place. ----
VDP1_ACCESSORS = """\
/* Expose VDP1's file-scope statics to the glue (C linkage). FB[!FBDrawWhich] is the
   displayed (front) bank; confirm against your build if the framebuffer looks stale. */
namespace MDFN_IEN_SS { namespace VDP1 {
extern "C" const unsigned short* SsDbgVdp1Vram(void) { return (const unsigned short*)VRAM; }
extern "C" const unsigned short* SsDbgVdp1Fb(void)   { return (const unsigned short*)FB[!FBDrawWhich]; }
/* Draw-end latch: a copy of VDP1 VRAM taken the instant the command list finishes
   plotting (SsDbgVdp1LatchDrawEnd is called from the draw-end path in the command loop).
   Saturn Explorer re-rasterizes the command table itself, so it must see the table as it
   was PLOTTED — not a half-rebuilt one captured at the later video-frame boundary. The glue
   prefers this latch; SsDbgVdp1Latch returns NULL until the first draw-end, so a fresh boot
   falls back to live VRAM. */
static unsigned short SsVdp1Latch[0x40000];   /* 512 KiB, matches VRAM[0x40000] */
static int SsVdp1LatchValid = 0;
extern "C" void SsDbgVdp1LatchDrawEnd(void) {
   memcpy(SsVdp1Latch, VRAM, sizeof SsVdp1Latch);
   SsVdp1LatchValid = 1;
}
extern "C" const unsigned short* SsDbgVdp1Latch(void) {
   return SsVdp1LatchValid ? SsVdp1Latch : (const unsigned short*)0;
}
extern "C" void SsDbgVdp1Regs(unsigned short o[11]) {
   o[0]=TVMR; o[1]=FBCR; o[2]=PTMR; o[3]=EWDR; o[4]=EWLR; o[5]=EWRR;
   o[6]=0;    o[7]=EDSR; o[8]=LOPR; o[9]=0;    o[10]=0;   /* ENDR/COPR/MODR write-only/computed */
}
}}"""

# Latch VDP1 VRAM the instant the command list finishes plotting (the normal draw-end: an
# END-bit command, not an abort/FB-swap interrupt). Injected into vdp1.cpp's command loop
# right after the "Drawing finished" completion. `extern "C"` is illegal inside a function
# body, so the call is plain and resolves to VDP1_DRAWEND_FWD (a file-scope forward decl
# prepended at BOF) — its C linkage matches the definition appended at EOF (VDP1_ACCESSORS).
VDP1_DRAWEND_FWD = 'extern "C" void SsDbgVdp1LatchDrawEnd(void);  /* SE_VDP1_LATCH_FWD */\n'
VDP1_DRAWEND_HOOK = (
    "    /* Saturn Explorer: latch VDP1 VRAM the instant the command list finishes plotting,\n"
    "       so the live command re-render matches what was drawn (see se_mednafen_glue.c). */\n"
    "    SsDbgVdp1LatchDrawEnd();\n"
)
# Anchored on the END-bit completion condition (unique in vdp1.cpp) + the DrawingActive/
# VRAMUsageEnd state transition — the stable, semantic markers of a *normal* draw finish.
# The `[^{}]*?` tolerates the debug log line in between, so a reworded/removed message won't
# break the match; a MISS is reported loudly via notes (apply_anchored) rather than silently.
VDP1_DRAWEND_ANCHOR = (
    r'(else if\(MDFN_UNLIKELY\(CommandData\[0\] & 0x8000\)\)\s*\{[^{}]*?'
    r'DrawingActive = false;\s*VRAMUsageEnd\(\);\s*\n)'
)

VDP2_ACCESSORS = """\
/* Expose VDP2's file-scope statics to the glue (C linkage). */
namespace MDFN_IEN_SS { namespace VDP2 {
extern "C" const unsigned short* SsDbgVdp2Vram(void) { return (const unsigned short*)VRAM; }
extern "C" const unsigned short* SsDbgCram(void)     { return (const unsigned short*)CRAM; }
extern "C" const unsigned short* SsDbgRawRegs(void)  { return (const unsigned short*)RawRegs; }
}}"""

SS_ACCESSORS = """\
/* Expose ss.cpp's file-scope statics + SH-2 cores to the glue (C linkage). */
namespace MDFN_IEN_SS {
extern "C" const unsigned short* SsDbgWramL(void) { return (const unsigned short*)WorkRAML; }
extern "C" const unsigned short* SsDbgWramH(void) { return (const unsigned short*)WorkRAMH; }
extern "C" void SsDbgSh2Regs(int cpu, unsigned int o[23]) {
   SH7095& c = CPU[cpu ? 1 : 0];
   for (int i = 0; i < 16; ++i) o[i] = c.GetRegister(SH7095::GSREG_R0 + i, 0, 0);
   o[16]=c.GetRegister(SH7095::GSREG_SR,0,0);   o[17]=c.GetRegister(SH7095::GSREG_GBR,0,0);
   o[18]=c.GetRegister(SH7095::GSREG_VBR,0,0);  o[19]=c.GetRegister(SH7095::GSREG_MACH,0,0);
   o[20]=c.GetRegister(SH7095::GSREG_MACL,0,0); o[21]=c.GetRegister(SH7095::GSREG_PR,0,0);
   /* TODO(mednafen): confirm the PC enumerator the disassembler should track. The
      SH7095 pipeline exposes GSREG_PC_IF (fetch stage), GSREG_PC_ID (decode), and
      GSREG_RPC; PC_IF is the usual "next instruction" for a running core. */
   o[22]=c.GetRegister(SH7095::GSREG_PC_IF,0,0);
}
extern "C" void SsDbgPokeByte(unsigned int addr, unsigned char val) {
   /* Route to Mednafen's own byte bus-write (used by the cheat engine): it does the
      writeability check + SH-2 cache invalidation and takes a Saturn bus address, so
      writing byte-by-byte preserves big-endian order — the conventional debugger poke,
      for any region (work RAM, VRAM, ...). If your fork lacks CheatMemWrite, point this
      at the equivalent bus/debug byte writer. Sound-RAM pokes (v13) also route here at
      the SCSP RAM bus base 0x25A00000 (see SeMdfnWriteSoundByte in the glue). */
   CheatMemWrite((unsigned int)addr, (unsigned char)val);
}
/* SsDbgSoundRam (v13, SCSP RAM read) and SsDbgScspSlots (v14, decoded voices) are NOT here:
   they need the `static SS_SCSP SCSP` instance and scsp.h's private Slots[]/SlotRegs[], which
   are only visible inside sound.cpp. They are injected there instead — see process_sound()
   (SOUND_ACCESSORS) + the SeDbgReadSlots member added to scsp.h (SCSP_SLOT_METHOD). */
/* Controller input injection (v7). SMPC_SetInjectedInput stores the translated pad
   state atomically; SMPC_UpdateInput overlays it after Mednafen refreshes host input. */
extern "C" void SsDbgSetPad(unsigned int port, unsigned int buttons) {
   SMPC_SetInjectedInput(port, buttons);
}
/* Read the 16-bit SH-2 instruction at a Saturn bus address (v9 shadow call stack). SH-2
   opcodes are big-endian in memory, so compose hi<<8|lo. Uses the same bus path as the
   poke (CheatMemRead is CheatMemWrite's read pair). TODO(mednafen): if your fork lacks
   CheatMemRead, point this at the equivalent debug byte reader; a fork that has the
   executing opcode on hand in the CPU dispatch can skip this and pass it to the hook. */
extern "C" unsigned short SsDbgReadOpcode(unsigned int addr) {
   unsigned char hi = CheatMemRead((unsigned int)addr);
   unsigned char lo = CheatMemRead((unsigned int)addr + 1);
   return (unsigned short)(((unsigned short)hi << 8) | (unsigned short)lo);
}
#ifdef WANT_DEBUGGER
/* Tier 3 — execution breakpoints + tracepoints via the ss debugger (needs a
   --enable-debugger build; without WANT_DEBUGGER these are no-ops below). Setting the
   CPU callback (DBG_SetCPUCallback) makes DBG_NeedCPUHooks() true, so ss.cpp's per-frame
   run-loop dispatcher (rltab[...][DBG_NeedCPUHooks()]) switches to the per-instruction
   DBG_CPUHandler path on its own. The single callback SeSsBpHook serves BOTH features:
   it runs SeMednafenTraceHook every call (tracepoints + the v9 shadow call stack), then
   halts if the SS debugger flagged a PC breakpoint at this instruction. During the
   callback DBG_CPUHandler guarantees which == DBG.ActiveCPU, so DBG.ActiveCPU is the
   executing CPU. */
static void SeSsBpHook(uint32 PC, bool bpoint);   /* fwd: SeSyncCpuHook installs it */
static int sSeBpActive = 0;      /* >=1 execution breakpoint installed */
static int sSeTraceActive = 0;   /* >=1 enabled tracepoint armed */
static int sSeStepActive = 0;    /* an instruction step is in progress */
/* Install/remove the per-instruction callback to match what is armed: continuous (every
   instruction) when tracepoints OR an instruction step are active so the hook sees every
   PC; non-continuous (fires only when the debugger finds a PC breakpoint) when only
   breakpoints exist; removed entirely when none is active so the fast run loop returns.
   Continuous still passes bpoint=true on breakpoint PCs, so it is a superset. */
static void SeSyncCpuHook(void) {
   if (sSeBpActive || sSeTraceActive || sSeStepActive)
      DBG_SetCPUCallback(SeSsBpHook, (sSeTraceActive || sSeStepActive) != 0);
   else
      DBG_SetCPUCallback(0, false);
}
static void SeSsBpHook(uint32 PC, bool bpoint) {
   /* Per-instruction (continuous mode): drive tracepoints + the shadow call stack. This
      runs BEFORE the halt gate, so a tracepoint on the very PC a breakpoint also halts on
      still fires as the PC is reached. */
   SeMednafenTraceHook((int)DBG.ActiveCPU, (unsigned int)PC);
   /* Instruction step: count this instruction on the stepped CPU; halt when the budget
      is spent (SeExportInsnStepTick returns 1). */
   int stepHalt = sSeStepActive ? SeExportInsnStepTick((int)DBG.ActiveCPU) : 0;
   if (bpoint || stepHalt) {
      if (stepHalt && !bpoint)
         SeExportNotifyStep((int)DBG.ActiveCPU, (unsigned int)PC);
      else
         SeExportNotifyStop((int)DBG.ActiveCPU, (unsigned int)PC);
      while (!SeExportGateFrame()) { }
      /* Gate released: set the callback mode for what runs next — continuous iff an
         instruction step (IST) was just requested, else it reverts to the bp/tracepoint
         arming. The mode during the spin above is inert (no instructions execute), so a
         single sync point here is enough. */
      sSeStepActive = SeExportInsnStepBegin() ? 1 : 0;
      SeSyncCpuHook();
   }
}
extern "C" void SsDbgAddExecBp(int cpu, unsigned int addr) {
   (void)cpu;   /* SS PC breakpoints are shared across both SH-2s */
   DBG_AddBreakPoint(BPOINT_PC, addr, addr, true);
   sSeBpActive = 1;
   SeSyncCpuHook();
}
/* Data (read/write) watchpoint over [addr, addr+size). kind bit0 = read, bit1 = write
   (1 read, 2 write, 3 read/write). The ss debugger checks these ranges per instruction
   in DBG_CPUHandler (CheckRWBreakpoints), comparing against the RAW effective address the
   instruction computes; a hit sets FoundBPoint and drives our CPU hook with bpoint=true,
   so SeSsBpHook halts exactly as it does for a PC breakpoint.

   The SH-2 sees the same RAM cell through several cache-region images (bits 31..29 select
   cached 0x0, cache-through 0x2, etc.), and games freely mix cached and uncached accesses
   to the same address. The front end sends a cached (0x0xxxxxxx) address, so we install the
   watchpoint in BOTH the cached and cache-through images to catch either access form. */
extern "C" void SsDbgAddMemBp(int cpu, unsigned int addr, unsigned int size, unsigned int kind) {
   (void)cpu;   /* SS data breakpoints are shared across both SH-2s */
   const unsigned int span = size ? size - 1u : 0u;
   const unsigned int images[2] = { addr & ~0x20000000u, addr | 0x20000000u };
   for (int i = 0; i < 2; ++i) {
      const unsigned int a = images[i];
      const unsigned int end = a + span;
      if (kind & 0x1u) DBG_AddBreakPoint(BPOINT_READ, a, end, true);
      if (kind & 0x2u) DBG_AddBreakPoint(BPOINT_WRITE, a, end, true);
   }
   sSeBpActive = 1;
   SeSyncCpuHook();
}
extern "C" void SsDbgClearBps(void) {
   DBG_FlushBreakPoints(BPOINT_PC);
   DBG_FlushBreakPoints(BPOINT_READ);
   DBG_FlushBreakPoints(BPOINT_WRITE);
   sSeBpActive = 0;
   SeSyncCpuHook();   /* keeps the continuous hook if tracepoints are still armed */
}
/* Arm/disarm per-instruction tracepoint scanning. Called by the glue whenever the
   tracepoint set changes (active = at least one enabled tracepoint). */
extern "C" void SsDbgSetTraceActive(int active) {
   sSeTraceActive = active ? 1 : 0;
   SeSyncCpuHook();
}
#else
extern "C" void SsDbgAddExecBp(int cpu, unsigned int addr) { (void)cpu; (void)addr; }
extern "C" void SsDbgAddMemBp(int cpu, unsigned int addr, unsigned int size, unsigned int kind) { (void)cpu; (void)addr; (void)size; (void)kind; }
extern "C" void SsDbgClearBps(void) {}
extern "C" void SsDbgSetTraceActive(int active) { (void)active; }
#endif
}"""

# ---- SCSP sound accessors. Injected into sound.cpp / scsp.h (NOT ss.cpp): the SCSP
#      instance is a file-scope static in sound.cpp, and the per-voice state lives in the
#      private Slots[]/SlotRegs[] members of SS_SCSP, so the read path must sit in that TU
#      with a public member on the class. Symbol/field names verified against
#      mednafen-git src/ss/{scsp.h,scsp.inc,sound.cpp} (Mednafen 1.32.1). ----

# A public member added to SS_SCSP (scsp.h), anchored after GetRAMPtr(). Being a member it
# can read the private Slots[]/SlotRegs[]; it writes the fixed 36-byte LE record documented
# in SeLiveProtocol.h. Read-only, so it can't perturb emulation.
SCSP_SLOT_METHOD = """\
 // Saturn Explorer live tap: serialize the 32 decoded voices into a 36-byte-per-record
 // little-endian block (layout in Integration/Common/SeLiveProtocol.h) for the Sound panel
 // and per-voice Play/Export. The LIVE fields (EnvLevel/EnvPhase/CurrentAddr) are the ones
 // that reveal which voices are actually sounding. Read-only.
 int SeDbgReadSlots(unsigned char* out)
 {
  // Little-endian u32 store (the u8 casts below truncate on their own, so no masks needed).
  auto w32 = [](unsigned char* p, uint32 v) {
   p[0] = (unsigned char)v;         p[1] = (unsigned char)(v >> 8);
   p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24);
  };
  for(unsigned i = 0; i < 32; i++)
  {
   unsigned char* r = out + i * 36;
   const Slot& s = Slots[i];
   const uint16 pv = SlotRegs[i][0x0B];               // DISDL/DIPAN/EFSDL/EFPAN register word
   const bool released = (s.EnvPhase == ENV_PHASE_RELEASE) && (s.EnvLevel >= 0x3FF);
   int oct = s.Octave & 0xF; if(oct & 0x8) oct -= 16; // OCT is 4-bit signed (-8..+7)
   r[0]  = s.KeyBit ? 1 : 0;
   r[1]  = (s.KeyBit && !released) ? 1 : 0;
   r[2]  = (unsigned char)s.EnvPhase;
   r[3]  = s.WF8Bit ? 1 : 0;
   r[4]  = (unsigned char)s.LoopMode;
   r[5]  = (unsigned char)oct;                         // sign-extended above; cast keeps low 8 bits
   r[6]  = (unsigned char)s.TotalLevel;
   r[7]  = (unsigned char)((pv >> 13) & 0x7);          // DISDL (direct send level)
   r[8]  = (unsigned char)((pv >>  8) & 0x1F);         // DIPAN (direct pan)
   r[9]  = (unsigned char)((pv >>  5) & 0x7);          // EFSDL (effect send level)
   r[10] = (unsigned char)(pv & 0x1F);                 // EFPAN (effect pan)
   r[11] = s.EnvRates[0]; r[12] = s.EnvRates[1];
   r[13] = s.EnvRates[2]; r[14] = s.EnvRates[3];
   r[15] = (unsigned char)s.DecayLevel;
   r[16] = (unsigned char)s.EnvLevel; r[17] = (unsigned char)(s.EnvLevel >> 8);
   r[18] = (unsigned char)s.FreqNum;  r[19] = (unsigned char)(s.FreqNum  >> 8);
   /* StartAddr is a byte offset into sound RAM; LoopStart/LoopEnd/CurrentAddr are in samples. */
   w32(r + 20, s.StartAddr); w32(r + 24, s.LoopStart);
   w32(r + 28, s.LoopEnd);   w32(r + 32, s.CurrentAddr);
  }
  return 32;
 }"""

# Free C-linkage accessors appended at EOF of sound.cpp, where the `static SS_SCSP SCSP`
# instance is in scope. Re-opens the namespace like the vdp1/vdp2 accessor blocks do.
#
# Guarded out of the SSF music player: ssf.cpp #includes sound.cpp with MDFN_SSFPLAY_COMPILE
# defined, and there the SCSP instance lives in namespace MDFN_IEN_SSFPLAY. Without the guard
# these accessors (a) fail to resolve `SCSP` inside the hard-coded MDFN_IEN_SS block, and
# (b) would define the same C-linkage symbols a second time, colliding with the SS core's at
# link. The SSF player has no use for the SE debug hooks, so it simply skips them.
SOUND_ACCESSORS = """\
/* Expose the SCSP instance (sound.cpp's `static SS_SCSP SCSP`) to the glue (C linkage).
   SsDbgSoundRam returns the 262144-word host-order SCSP RAM (the glue SwapU16ToBE's it to
   big-endian for the wire); SsDbgScspSlots serializes the 32 decoded voices via the
   SeDbgReadSlots member added to scsp.h.
   Skipped for the SSF player build: ssf.cpp #includes sound.cpp with MDFN_SSFPLAY_COMPILE
   defined, where SCSP is in MDFN_IEN_SSFPLAY and these C-linkage symbols would also clash
   with the SS core's at link. */
#ifndef MDFN_SSFPLAY_COMPILE
namespace MDFN_IEN_SS {
extern "C" const unsigned short* SsDbgSoundRam(void) { return (const unsigned short*)SCSP.GetRAMPtr(); }
extern "C" int SsDbgScspSlots(unsigned char* out) { return SCSP.SeDbgReadSlots(out); }
}
#endif"""

# scsp.h anchor: insert the member right after the public GetRAMPtr() accessor.
SCSP_RAMPTR_ANCHOR = r'(INLINE\s+uint16\*\s+GetRAMPtr\(void\)\s*\{\s*return\s+RAM;\s*\}\s*\n)'

# Live CD-block status accessor (v15) appended at EOF of cdb.cpp, where the file-static drive
# state (CurPosInfo, CurPlayStart/End) is in scope. Serializes the 16-byte wire record
# little-endian to match SeLiveProtocol's se_cd_status layout (the glue passes it through
# verbatim): current_fad(u32) play_start_fad(u32) play_end_fad(u32) status(u8) pad(3).
# CurPosInfo.status is the Saturn CD-block status byte (STATUS_* in cdb.cpp); we fold its
# transient high bits off (& 0x0F) and map the base code to Saturn Explorer's SE_CD_* enum
# (0 idle / 1 seek / 2 read / 3 play / 4 pause / 5 scan). STATUS_PLAY is the state during
# active sector streaming (data reads and CD-DA both), which is exactly what the Disc Explorer
# wants to resolve to a file — so it maps to "read".
CD_ACCESSORS = """\
/* Saturn Explorer live CD-block status (v15). Reads cdb.cpp's file-static drive state and
   serializes the 16-byte se_cd_status wire record little-endian. See apply.py for the
   status-byte -> SE_CD_* mapping rationale. */
namespace MDFN_IEN_SS {
extern "C" int SsDbgCdStatus(unsigned char* out)
{
 auto w32 = [](unsigned char* p, uint32 v) {
  p[0] = (unsigned char)v;         p[1] = (unsigned char)(v >> 8);
  p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24);
 };
 /* STATUS_BUSY..STATUS_FATAL (0x00..0x0A) -> SE_CD_* (idle/seek/read/play/pause/scan). */
 static const unsigned char kMap[11] = {
  1, /* BUSY    -> seek  */  4, /* PAUSE   -> pause */  0, /* STANDBY -> idle */
  2, /* PLAY    -> read  */  1, /* SEEK    -> seek  */  5, /* SCAN    -> scan */
  0, /* OPEN    -> idle  */  0, /* NODISC  -> idle  */  1, /* RETRY   -> seek */
  0, /* ERROR   -> idle  */  0  /* FATAL   -> idle  */
 };
 const unsigned base = CurPosInfo.status & 0x0F;
 w32(out + 0, CurPosInfo.fad);
 w32(out + 4, CurPlayStart);
 w32(out + 8, CurPlayEnd);
 out[12] = (base < 11) ? kMap[base] : 0;
 out[13] = out[14] = out[15] = 0;
 return 1;
}
}"""

# Full-savestate save/load accessors (v16 rewind), appended at EOF of ss.cpp. se_export's worker
# thread delta-compresses the saved states off the emulate thread, so these need only produce /
# consume a full state image. Shipped as a COMPILING STUB (feature dormant) by default: the exact
# Mednafen savestate-to-memory API (MDFNSS_SaveSM / MDFNSS_LoadSM against a MemoryStream, and
# whether they live in the global or Mednafen:: namespace) varies by fork and must be confirmed on
# the actual tree. Build with -DSE_MDFN_REWIND=1 after wiring the real calls (see the block below)
# to enable rewind; until then SsDbgSaveState returns 0, which leaves the ring empty and the
# feature off — everything else (protocol, client, UI) already degrades gracefully.
SAVESTATE_ACCESSORS = """\
/* Saturn Explorer full-savestate save/load (v16 rewind). Dormant by default (stub); build with
   -DSE_MDFN_REWIND=1 to enable the real MDFNSS memory-stream path below. If your fork puts
   MDFNSS_SaveSM/LoadSM or MemoryStream in a different namespace, or names the size/map/rewind
   accessors differently, adjust the enabled branch — the stub keeps the tree compiling meanwhile.
   se_export's worker delta-compresses these full states off the emulate thread. */
#ifdef SE_MDFN_REWIND
#include <mednafen/state.h>
#include <mednafen/MemoryStream.h>
extern "C" size_t SsDbgSaveState(unsigned char* buf, size_t cap) {
 try { Mednafen::MemoryStream ms; Mednafen::MDFNSS_SaveSM(&ms, true);   /* data_only: no preview */
       uint64 sz = ms.size(); if(!buf) return (size_t)sz;
       if((uint64)cap < sz) return 0; memcpy(buf, ms.map(), (size_t)sz); return (size_t)sz;
 } catch(...) { return 0; } }
extern "C" int SsDbgLoadState(const unsigned char* buf, size_t len) {
 try { Mednafen::MemoryStream ms(len?len:1, 0); if(len) memcpy(ms.map(), buf, len); ms.rewind();
       Mednafen::MDFNSS_LoadSM(&ms, true); return 0; } catch(...) { return -1; } }
#else
extern "C" size_t SsDbgSaveState(unsigned char* buf, size_t cap) { (void)buf; (void)cap; return 0; }
extern "C" int    SsDbgLoadState(const unsigned char* buf, size_t len) { (void)buf; (void)len; return -1; }
#endif"""

SMPC_INPUT_DECL = """\
/* Saturn Explorer controller injection. `buttons` is the protocol's SE_PAD_* mask;
   the SMPC implementation translates and overlays it on the host gamepad state. */
void SMPC_SetInjectedInput(unsigned port, uint32 buttons);
"""

SMPC_INPUT_STATE = """\
/* Saturn Explorer controller injection. The server thread writes these masks while
   the emulation thread consumes them, so keep the handoff atomic. Values use
   Mednafen's digital-pad data-buffer bit order, which is the ENTRY POSITION in
   IODevice_Gamepad_IDII (input/gamepad.cpp) -- NOT the number in each entry's
   IDIIS_Button(...) third argument, which is only the config-prompt order:
     0 Z, 1 Y, 2 X, 3 R, 4 UP, 5 DOWN, 6 LEFT, 7 RIGHT,
     8 B, 9 C, 10 A, 11 START, 12-14 padding, 15 L
   (Confirmed against the emulator's own host input: pressing Right in Mednafen's
   window sets 0x0080 = bit 7.) */
static std::atomic_uint_least16_t SeInjectedPad[2];

extern "C" void SeExportLog(const char* msg);   /* diagnostic log -> SE Log window (v11+) */

void SMPC_SetInjectedInput(unsigned port, uint32 buttons)
{
 if(port >= 2) return;
 /* SE_PAD_* -> data-buffer bit, per the IDII entry order documented above. Spelled out
    as a table rather than shift arithmetic: the layout interleaves face buttons and
    directions, so there is no clean shift, and a wrong bit silently presses a different
    button (this previously mapped START onto bit 4, which is UP). */
 static const struct { uint16 se; uint8 bit; } se_to_pad[13] = {
  { 0x0001u,  4 },   /* UP    */  { 0x0002u,  5 },   /* DOWN  */
  { 0x0004u,  6 },   /* LEFT  */  { 0x0008u,  7 },   /* RIGHT */
  { 0x0010u, 10 },   /* A     */  { 0x0020u,  8 },   /* B     */
  { 0x0040u,  9 },   /* C     */  { 0x0080u,  2 },   /* X     */
  { 0x0100u,  1 },   /* Y     */  { 0x0200u,  0 },   /* Z     */
  { 0x0400u, 15 },   /* L     */  { 0x0800u,  3 },   /* R     */
  { 0x1000u, 11 },   /* START */
 };
 uint16 native = 0;
 for(unsigned i = 0; i < 13; i++)
  if(buttons & se_to_pad[i].se) native |= (uint16)(1u << se_to_pad[i].bit);
 const uint16 prev = SeInjectedPad[port].exchange(native, std::memory_order_relaxed);
 /* Diagnostic: what the SMPC translate produced. Only on change — the LiveDriver poll
    thread re-sends INP every cycle while a button is held (to cover a non-latching
    glue), so logging every call would flood SE's Log window at poll rate. */
 if(native != prev)
 {
  char m[80];
  snprintf(m, sizeof(m), "SMPC inject: port=%u se=0x%04X -> native=0x%04X",
           port, (unsigned)(buttons & 0x1FFFu), (unsigned)native);
  SeExportLog(m);
 }
}

/* Report what the emulated port currently sees, so Saturn Explorer can verify its
   injection actually reaches the pad. out[0] = device kind (0 none, 1 gamepad,
   2 3D pad, 3 other), out[1] = host input bits, out[2] = SE-injected bits. */
extern "C" void SsDbgQueryInput(unsigned port, unsigned int out[3])
{
 out[0] = out[1] = out[2] = 0;
 if(port >= 2) return;
 const unsigned vp = port;                 /* no-multitap: physical port == virtual port */
 IODevice* const dev = VirtualPorts[vp];
 if(dev == &PossibleDevices[vp].gamepad)        out[0] = 1;
 else if(dev == &PossibleDevices[vp].threedpad) out[0] = 2;
 else if(dev == &PossibleDevices[vp].none)      out[0] = 0;
 else                                           out[0] = 3;
 const uint8* d = VirtualPortsDPtr[vp];
 out[1] = d ? (unsigned)(d[0] | ((uint16)d[1] << 8)) : 0u;
 out[2] = (unsigned)SeInjectedPad[vp].load(std::memory_order_relaxed);
}

/* Human-readable device type for a port, so Saturn Explorer can log the emulator's
   controller configuration when a client connects. Ports 0/1 map to VirtualPorts[0/1]
   in the no-multitap case (physical port == virtual port). */
extern "C" const char* SsDbgPortDeviceName(unsigned port)
{
 if(port >= 2) return "N/A";
 IODevice* const dev = VirtualPorts[port];
 if(dev == &PossibleDevices[port].none)        return "None (disconnected)";
 if(dev == &PossibleDevices[port].gamepad)     return "Digital Control Pad";
 if(dev == &PossibleDevices[port].threedpad)   return "3D Control Pad";
 if(dev == &PossibleDevices[port].mouse)       return "Mouse";
 if(dev == &PossibleDevices[port].wheel)       return "Arcade Racer (Wheel)";
 if(dev == &PossibleDevices[port].mission)     return "Mission Stick";
 if(dev == &PossibleDevices[port].dualmission) return "Twin Mission Stick";
 if(dev == &PossibleDevices[port].gun)         return "Virtua Gun / Stunner";
 if(dev == &PossibleDevices[port].keyboard)    return "Keyboard";
 if(dev == &PossibleDevices[port].jpkeyboard)  return "JP Keyboard";
 return "Unknown/other";
}

/* Saturn Explorer host-input diagnostics. The SDL frontend's key bindings live in
   drivers/input.cpp; this reports the scancode bound to each Saturn button so a host
   keypress can be named in the log rather than shown as a bare number. */
extern "C" int SsDbgQueryKeyMap(unsigned port, int out[13]);

static void SeKeyName(int sc, char* out, size_t n)
{
 if(sc < 0)                  { snprintf(out, n, "unbound"); return; }
 if(sc >= 4  && sc <= 29)    { snprintf(out, n, "%c", 'A' + (sc - 4)); return; }
 if(sc >= 30 && sc <= 38)    { snprintf(out, n, "%d", 1 + (sc - 30)); return; }
 if(sc >= 58 && sc <= 69)    { snprintf(out, n, "F%d", 1 + (sc - 58)); return; }
 switch(sc)
 {
  case  39: snprintf(out, n, "0");         return;
  case  40: snprintf(out, n, "Enter");     return;
  case  41: snprintf(out, n, "Esc");       return;
  case  42: snprintf(out, n, "Backspace"); return;
  case  43: snprintf(out, n, "Tab");       return;
  case  44: snprintf(out, n, "Space");     return;
  case  79: snprintf(out, n, "Right");     return;
  case  80: snprintf(out, n, "Left");      return;
  case  81: snprintf(out, n, "Down");      return;
  case  82: snprintf(out, n, "Up");        return;
  case 224: snprintf(out, n, "LCtrl");     return;
  case 225: snprintf(out, n, "LShift");    return;
  case 226: snprintf(out, n, "LAlt");      return;
  default:  snprintf(out, n, "sc%d", sc);  return;
 }
}

static void SeSMPCUpdateInput(unsigned vp, const int32 time_elapsed)
{
 uint8* data = VirtualPortsDPtr[vp];
 uint8 merged[10];
 if(vp < 2 && data)
 {
  const uint16 inj = SeInjectedPad[vp].load(std::memory_order_relaxed);
  const bool ispad = (VirtualPorts[vp] == &PossibleDevices[vp].gamepad);
  const bool is3d  = (VirtualPorts[vp] == &PossibleDevices[vp].threedpad);
  /* Diagnostic: log every change of the INJECTED mask — including the release edge and
     the case where the port is not a pad at all, which is the one situation where an
     injection is silently dropped. Keyed on the injected mask rather than the merged
     result: the merged value was only recomputed inside the branch below, so a release
     (inj == 0) skipped it and left the last merged value latched, which meant a repeat
     press of the same button never logged again after the first one. */
  const uint16 hostBits = (uint16)(data[0] | ((uint16)data[1] << 8));
  /* Host input: what Mednafen received DIRECTLY from its own keyboard / host gamepad
     (before any SE injection is merged in), and which Saturn button each bit is. Named
     via the SDL driver's key bindings, so pressing Up in Mednafen's own window logs
     "Up" together with the UP d-pad it was handled as. On change only. */
  static uint16 sLastHost[2] = { 0xFFFFu, 0xFFFFu };
  if(hostBits != sLastHost[vp])
  {
   /* Data-buffer bit order = IDII entry order (input/gamepad.cpp); see the note on
      SMPC_SetInjectedInput. Bits 12-14 are padding and never named. */
   static const char* const bn[16] =
    { "Z","Y","X","R","UP","DOWN","LEFT","RIGHT","B","C","A","START","-","-","-","L" };
   /* SsDbgQueryKeyMap reports in SE_PAD_* order (up,down,left,right,a,b,c,x,y,z,ls,rs,
      start); map each of those slots to the data-buffer bit above. */
   static const int km2data[13] = { 4,5,6,7, 10,8,9, 2,1,0, 15,3, 11 };
   int km[13], i;
   if(!SsDbgQueryKeyMap(vp, km)) { for(i = 0; i < 13; i++) km[i] = -1; }
   char names[176]; unsigned pos = 0; names[0] = 0;
   for(unsigned b = 0; b < 16 && pos + 32 < sizeof(names); b++)
    if(hostBits & (1u << b))
    {
     int sc = -1;
     for(i = 0; i < 13; i++) if(km2data[i] == (int)b) { sc = km[i]; break; }
     char kn[24];
     SeKeyName(sc, kn, sizeof(kn));
     pos += snprintf(names + pos, sizeof(names) - pos, "%s%s[%s]", pos ? "+" : "", bn[b], kn);
    }
   char hm[224];
   snprintf(hm, sizeof(hm), "host input: port=%u host=0x%04X -> %s",
            vp, (unsigned)hostBits, names[0] ? names : "(all released)");
   SeExportLog(hm);
   sLastHost[vp] = hostBits;
  }
  static uint16 sLastInj[2] = { 0xFFFFu, 0xFFFFu };
  if(inj != sLastInj[vp])
  {
   const uint16 h = hostBits;
   char m[128];
   snprintf(m, sizeof(m), "pad merge: port=%u inj=0x%04X host=0x%04X -> pad=0x%04X (%s)",
            vp, (unsigned)inj, (unsigned)h, (unsigned)((ispad || is3d) ? (h | inj) : h),
            ispad ? "gamepad" : (is3d ? "3D pad" : "NOT A PAD - injection ignored"));
   SeExportLog(m);
   sLastInj[vp] = inj;
  }
  if(inj && ispad)
  {
   /* Digital Control Pad: 2-byte data buffer, digital bits at [0..1]. */
   const uint16 host = (uint16)(data[0] | ((uint16)data[1] << 8));
   const uint16 combined = (uint16)(host | inj);
   merged[0] = (uint8)combined;
   merged[1] = (uint8)(combined >> 8);
   data = merged;
  }
  else if(inj && is3d)
  {
   /* 3D Control Pad: 10-byte buffer (input/3dpad.cpp) — digital bits at [0..1] (bits
      0..10 share the gamepad layout), analog stick at [2..5], analog shoulders at
      [6..9]. OR the digital bits in, and drive L/R as full analog when injected so
      shoulder games respond. Mode + stick are left as the host provides. */
   memcpy(merged, data, 10);
   /* Digital bits share the gamepad layout: 0..11 plus L at 15 (12..14 are padding). */
   const uint16 dtmp = (uint16)((data[0] | ((uint16)data[1] << 8)) | (inj & 0x8FFFu));
   merged[0] = (uint8)dtmp;
   merged[1] = (uint8)(dtmp >> 8);
   if(inj & (1u << 15)) { merged[6] = 0xFF; merged[7] = 0xFF; } /* L shoulder analog full */
   if(inj & (1u <<  3)) { merged[8] = 0xFF; merged[9] = 0xFF; } /* R shoulder analog full */
   data = merged;
  }
 }
 VirtualPorts[vp]->UpdateInput(data, time_elapsed);
}
"""

SMPC_INPUT_UPDATE = """\
  SeSMPCUpdateInput(vp, time_elapsed);
"""

# Files that get an accessor block appended at EOF: (filename, block, key). ss.cpp
# also gets accessors, but via process_ss (it has hook injections too).
APPEND_EDITS = [
    ("vdp1.cpp", VDP1_ACCESSORS, "SsDbgVdp1Vram"),
    ("vdp2.cpp", VDP2_ACCESSORS, "SsDbgVdp2Vram"),
]

# File-scope forward declarations (extern "C" is illegal inside a function body, so
# the call sites below are plain calls that resolve to these). Prepended at BOF.
FWD_DECLS = (
    "extern \"C\" void SeMednafenFrameHook(void);\n"
    "extern \"C\" int  SeExportGateFrame(void);\n"
    "extern \"C\" void SeExportNotifyStop(int cpu, unsigned int pc);\n"
    "extern \"C\" void SeExportNotifyStep(int cpu, unsigned int pc);\n"
    "extern \"C\" int  SeExportInsnStepBegin(void);\n"
    "extern \"C\" int  SeExportInsnStepTick(int cpu);\n"
    "extern \"C\" void SeMednafenTraceHook(int cpu, unsigned int pc);\n"
)

# Per-frame snapshot call, injected after the frame's cycle count is finalized.
FRAME_HOOK = (
    "   /* Saturn Explorer live tap: publish this frame (lazy-starts the server). */\n"
    "   SeMednafenFrameHook();\n"
)
FRAME_ANCHOR = r'(espec->MasterCycles\s*=\s*[^;]*;\s*\n)'

# Optional pause/step gate at the top of Emulate() (opt-in: can disturb audio timing).
GATE_HOOK = (
    "   /* Saturn Explorer pause/step gate (remove if it disturbs audio timing). */\n"
    "   while (!SeExportGateFrame()) { }\n"
)
GATE_ANCHOR = r'(\bEmulate\s*\(\s*EmulateSpecStruct\s*\*\s*\w+\s*\)\s*\{\s*\n)'

# Window-title mark: append "(SaturnExplorer Enabled. <ver> / Mednafen <rev>)" to the
# SDL window title so a tapped build is obvious. This lives in the SDL frontend
# (src/drivers/video.cpp), not the ss core; skipped gracefully if that file isn't
# present (a non-SDL / libretro build). Forward-decl at BOF (extern "C" can't sit in a
# function body); the append goes right after Mednafen's own SDL_SetWindowTitle.
TITLE_FWD = 'extern "C" const char* SeExportTitleSuffix(const char*, const char*);\n'
TITLE_HOOK = (
    "  /* Saturn Explorer: mark this window as tapped. */\n"
    "  { char se_t[256]; SDL_snprintf(se_t, sizeof se_t, \"%s %s\", SDL_GetWindowTitle(window),\n"
    "      SeExportTitleSuffix(\"Mednafen\", MEDNAFEN_VERSION)); SDL_SetWindowTitle(window, se_t); }\n"
)
TITLE_ANCHOR = r'(SDL_SetWindowTitle\(window,[^;]*;\s*\n)'
TITLE_FILE = os.path.join("src", "drivers", "video.cpp")

# Live host keyboard bindings: report which keyboard scancode the user has mapped to each
# Saturn pad button, read from the driver-layer PIDC[] binding cache (the same data
# mednafen.cfg is serialized from). This lets a front end mirror the user's keys live —
# no config-file export/upload. The cache lives in the SDL frontend (src/drivers/input.cpp,
# where PIDC / ButtonInfoCache / ButtConfig are all in scope), NOT the ss core, so — like
# the window-title mark — this is appended to a drivers-layer file, best-effort (a non-SDL
# build won't have it). Fills out[13] with USB-HID scancodes in ascending SE_PAD_* order
# (UP,DOWN,LEFT,RIGHT,A,B,C,X,Y,Z,L,R,START), -1 where no keyboard key is bound. Matching is
# by the device-agnostic IDII setting token, so it serves the gamepad and 3D Control Pad alike.
INPUT_ACCESSORS = r'''extern "C" int SsDbgQueryKeyMap(unsigned port, int out[13])
{
   static const char* const se_tok[13] = {
      "up", "down", "left", "right", "a", "b", "c", "x", "y", "z", "ls", "rs", "start" };
   int i, matched = 0;
   for(i = 0; i < 13; i++) out[i] = -1;
   if(port >= 16) return 0;
   for(i = 0; i < 13; i++)
   {
      size_t j;
      for(j = 0; j < PIDC[port].BIC.size(); j++)
      {
         const ButtonInfoCache& bic = PIDC[port].BIC[j];
         size_t k;
         if(!bic.IDII || !bic.IDII->SettingName) continue;
         if(strcmp(bic.IDII->SettingName, se_tok[i]) != 0) continue;
         for(k = 0; k < bic.BC.size(); k++)
            if(bic.BC[k].DeviceType == BUTTC_KEYBOARD)
            {
               out[i] = (int)(bic.BC[k].ButtonNum & 0x0FFF);   /* low 12 bits = scancode */
               matched++;
               break;
            }
         break;
      }
   }
   return matched;
}'''
INPUT_FILE = os.path.join("src", "drivers", "input.cpp")

FENCE_RE = re.compile(re.escape(BEGIN) + r".*?" + re.escape(END), re.DOTALL)


def find_src(root):
    for cand in (os.path.join(root, "src", "ss"), os.path.join(root, "ss"), root):
        if os.path.isfile(os.path.join(cand, "ss.cpp")):
            return cand
    return None


# All three placements share one idempotency rule (each fenced block is identified by
# a stable `key` substring, unique across the blocks in a file): if a block carrying
# `key` already exists, replace it in place when its content changed and no-op when it
# matches; otherwise place a fresh block. `place(text, block)` performs the placement
# for a new block and returns the new text, or None if it can't (e.g. anchor missing).
def upsert(text, code, key, place):
    block = fence(code.rstrip("\n"))
    existing = next((m.group(0) for m in FENCE_RE.finditer(text) if key in m.group(0)), None)
    if existing is not None:
        if existing == block:
            return text, f"  ok (already)  {key}"
        return text.replace(existing, block, 1), f"  ~ updated     {key}"
    placed = place(text, block)
    if placed is None:
        return text, f"  ANCHOR MISS   {key}  <-- add by hand (see README)"
    return placed, f"  + applied     {key}"


def apply_anchored(text, anchor, code, key):
    """Insert fenced `code` after `anchor` (or ANCHOR MISS if it isn't found)."""
    def place(t, block):
        m = re.search(anchor, t)
        return t[:m.end(1)] + block + "\n" + t[m.end(1):] if m else None
    return upsert(text, code, key, place)


def apply_append(text, code, key):
    """Append fenced `code` at EOF."""
    return upsert(text, code, key,
                  lambda t, block: t + ("" if t.endswith("\n") else "\n") + "\n" + block + "\n")


def apply_prepend(text, code, key):
    """Prepend fenced `code` at BOF."""
    return upsert(text, code, key, lambda t, block: block + "\n" + t)


def apply_replace(text, anchor, code, key):
    """Replace one regex match with fenced `code` (or update its existing fence)."""
    def place(t, block):
        m = re.search(anchor, t)
        return t[:m.start()] + block + t[m.end():] if m else None
    return upsert(text, code, key, place)


def process_ss(src_dir, do_write, with_pause):
    path = os.path.join(src_dir, "ss.cpp")
    if not os.path.isfile(path):
        return ["MISSING  ss.cpp"]
    text = original = open(path, encoding="utf-8", errors="surrogateescape").read()
    notes = ["ss.cpp:"]
    text, n = apply_prepend(text, FWD_DECLS, "SeMednafenFrameHook(void)")
    notes.append(n)
    text, n = apply_anchored(text, FRAME_ANCHOR, FRAME_HOOK, "SeMednafenFrameHook();")
    notes.append(n)
    if with_pause:
        text, n = apply_anchored(text, GATE_ANCHOR, GATE_HOOK, "while (!SeExportGateFrame())")
        notes.append(n)
    text, n = apply_append(text, SS_ACCESSORS, "SsDbgWramL")
    notes.append(n)
    text, n = apply_append(text, SAVESTATE_ACCESSORS, "SsDbgSaveState")
    notes.append(n)
    if do_write and text != original:
        open(path, "w", encoding="utf-8", errors="surrogateescape").write(text)
    return notes


def process_vdp1_drawend(src_dir, do_write):
    """Inject the VDP1 draw-end latch call into vdp1.cpp's command loop (the accessor block
    itself is appended via APPEND_EDITS). Anchored on the normal 'Drawing finished'
    completion so an aborted/interrupted draw doesn't latch a partial command table."""
    path = os.path.join(src_dir, "vdp1.cpp")
    if not os.path.isfile(path):
        return ["vdp1.cpp (draw-end latch):", "  MISSING  vdp1.cpp"]
    notes = ["vdp1.cpp (draw-end latch):"]
    text = original = open(path, encoding="utf-8", errors="surrogateescape").read()
    text, n = apply_prepend(text, VDP1_DRAWEND_FWD, "SE_VDP1_LATCH_FWD")
    notes.append(n)
    text, n = apply_anchored(text, VDP1_DRAWEND_ANCHOR, VDP1_DRAWEND_HOOK, "SsDbgVdp1LatchDrawEnd();")
    notes.append(n)
    if do_write and text != original:
        open(path, "w", encoding="utf-8", errors="surrogateescape").write(text)
    return notes


def process_sound(src_dir, do_write):
    """Wire the SCSP read path (sound RAM + decoded voices). These accessors need the
    `static SS_SCSP SCSP` instance and scsp.h's private Slots[]/SlotRegs[], both visible
    only inside sound.cpp — so a public SeDbgReadSlots member goes into scsp.h and the two
    C-linkage accessors are appended to sound.cpp."""
    notes = ["sound.cpp / scsp.h:"]
    h_path = os.path.join(src_dir, "scsp.h")
    cpp_path = os.path.join(src_dir, "sound.cpp")
    if not os.path.isfile(h_path) or not os.path.isfile(cpp_path):
        return notes + ["  MISSING  scsp.h or sound.cpp"]

    text = original = open(h_path, encoding="utf-8", errors="surrogateescape").read()
    text, n = apply_anchored(text, SCSP_RAMPTR_ANCHOR, SCSP_SLOT_METHOD, "SeDbgReadSlots")
    notes.append(n)
    if do_write and text != original:
        open(h_path, "w", encoding="utf-8", errors="surrogateescape").write(text)

    text = original = open(cpp_path, encoding="utf-8", errors="surrogateescape").read()
    text, n = apply_append(text, SOUND_ACCESSORS, "SsDbgScspSlots")
    notes.append(n)
    if do_write and text != original:
        open(cpp_path, "w", encoding="utf-8", errors="surrogateescape").write(text)
    return notes


def process_cd(src_dir, do_write):
    """Wire the live CD-block status read path (v15). The drive state (CurPosInfo, CurPlayStart/
    End) is file-static in cdb.cpp, so the C-linkage accessor is simply appended there — it needs
    no header edit. cdb.cpp is SS-only (unlike sound.cpp, which the SSF player also includes), so
    no MDFN_SSFPLAY_COMPILE guard is required."""
    cpp_path = os.path.join(src_dir, "cdb.cpp")
    notes = ["cdb.cpp:"]
    if not os.path.isfile(cpp_path):
        return notes + ["  MISSING  cdb.cpp"]
    text = original = open(cpp_path, encoding="utf-8", errors="surrogateescape").read()
    text, n = apply_append(text, CD_ACCESSORS, "SsDbgCdStatus")
    notes.append(n)
    if do_write and text != original:
        open(cpp_path, "w", encoding="utf-8", errors="surrogateescape").write(text)
    return notes


def process_smpc(src_dir, do_write):
    cpp_path = os.path.join(src_dir, "smpc.cpp")
    h_path = os.path.join(src_dir, "smpc.h")
    notes = ["smpc.cpp / smpc.h:"]
    if not os.path.isfile(cpp_path) or not os.path.isfile(h_path):
        return notes + ["  MISSING  smpc.cpp or smpc.h"]

    text = original = open(cpp_path, encoding="utf-8", errors="surrogateescape").read()
    # Early versions of this hook prepended <atomic>, but Mednafen requires types.h
    # (pulled in by ss.h) to precede standard integer headers. Remove that exact legacy
    # fence once, then keep the include anchored immediately after ss.h.
    legacy_atomic = fence("#include <atomic>") + "\n"
    text = text.replace(legacy_atomic, "", 1)
    text, n = apply_anchored(text, r'(#include "ss\.h"\s*\n)',
                             "#include <atomic>  /* SE_SMPC_ATOMIC_INCLUDE */",
                             "SE_SMPC_ATOMIC_INCLUDE")
    notes.append(n)
    text, n = apply_anchored(text, r'(static uint8\* MiscInputPtr;\s*\n)',
                             SMPC_INPUT_STATE, "SeInjectedPad[2]")
    notes.append(n)
    text, n = apply_replace(
        text,
        r'[ \t]*VirtualPorts\[vp\]->UpdateInput\(VirtualPortsDPtr\[vp\], time_elapsed\);',
        SMPC_INPUT_UPDATE, "SeSMPCUpdateInput(vp, time_elapsed)")
    notes.append(n)
    if do_write and text != original:
        open(cpp_path, "w", encoding="utf-8", errors="surrogateescape").write(text)

    text = original = open(h_path, encoding="utf-8", errors="surrogateescape").read()
    text, n = apply_anchored(text, r'(void SMPC_SetInput\([^;]+;\s*\n)',
                             SMPC_INPUT_DECL, "SMPC_SetInjectedInput")
    notes.append(n)
    if do_write and text != original:
        open(h_path, "w", encoding="utf-8", errors="surrogateescape").write(text)
    return notes


def process_append_file(src_dir, fname, block, key, do_write):
    path = os.path.join(src_dir, fname)
    if not os.path.isfile(path):
        return [f"MISSING  {fname}"]
    text = original = open(path, encoding="utf-8", errors="surrogateescape").read()
    text, note = apply_append(text, block, key)
    if do_write and text != original:
        open(path, "w", encoding="utf-8", errors="surrogateescape").write(text)
    return [f"{fname}:", note]


def process_title(root, do_write):
    """Append the SaturnExplorer mark to the SDL window title (src/drivers/video.cpp).
    Optional/best-effort: a non-SDL or libretro build won't have this file — skip it."""
    path = os.path.join(root, TITLE_FILE)
    if not os.path.isfile(path):
        return [f"{TITLE_FILE}:", "  (not found — window-title mark skipped; non-SDL build?)"]
    text = original = open(path, encoding="utf-8", errors="surrogateescape").read()
    notes = [f"{TITLE_FILE}:"]
    text, n = apply_prepend(text, TITLE_FWD, "SeExportTitleSuffix(const char*")
    notes.append(n)
    text, n = apply_anchored(text, TITLE_ANCHOR, TITLE_HOOK, 'SeExportTitleSuffix("Mednafen"')
    notes.append(n)
    if do_write and text != original:
        open(path, "w", encoding="utf-8", errors="surrogateescape").write(text)
    return notes


def process_input(root, do_write):
    """Append the SsDbgQueryKeyMap accessor to the SDL input driver (src/drivers/input.cpp).
    Optional/best-effort: a non-SDL or libretro build won't have this file — skip it."""
    path = os.path.join(root, INPUT_FILE)
    if not os.path.isfile(path):
        return [f"{INPUT_FILE}:", "  (not found — keyboard-map hook skipped; non-SDL build?)"]
    text = original = open(path, encoding="utf-8", errors="surrogateescape").read()
    text, note = apply_append(text, INPUT_ACCESSORS, "SsDbgQueryKeyMap")
    if do_write and text != original:
        open(path, "w", encoding="utf-8", errors="surrogateescape").write(text)
    return [f"{INPUT_FILE}:", note]


def write_if_changed(destpath, data):
    """Write `data` (bytes) to destpath only when it differs from what's already there,
    so re-copying an UNCHANGED source doesn't bump the file's mtime and force `make` to
    recompile it. This is what makes a re-run incremental: only Common/glue files whose
    content actually changed get touched. Returns 'copy' | 'update' | 'unchanged'."""
    existed = os.path.isfile(destpath)
    if existed:
        try:
            with open(destpath, "rb") as f:
                if f.read() == data:
                    return "unchanged"
        except OSError:
            pass
    with open(destpath, "wb") as f:
        f.write(data)
    return "update" if existed else "copy"


def copy_sources(src_dir, do_write):
    notes = []
    for name in COMMON_FILES:
        srcpath = os.path.join(SHARED_DIR, name)
        if not os.path.isfile(srcpath):
            notes.append(f"  MISSING SOURCE {srcpath}")
            continue
        with open(srcpath, "rb") as f:
            data = f.read()
        status = write_if_changed(os.path.join(src_dir, name), data) if do_write else "copy"
        notes.append(f"  {status:<11} {name}")
    # Glue: copy with SE_MEDNAFEN_WIRED defined so the snapshot path compiles.
    glue_src = os.path.join(HERE, GLUE_FILE)
    if os.path.isfile(glue_src):
        with open(glue_src, "rb") as f:
            data = b"#define SE_MEDNAFEN_WIRED 1\n" + f.read()
        status = write_if_changed(os.path.join(src_dir, GLUE_FILE), data) if do_write else "copy"
        notes.append(f"  {status:<11} {GLUE_FILE} (+SE_MEDNAFEN_WIRED)")
    else:
        notes.append(f"  MISSING SOURCE {glue_src}")
    return notes


# Build wiring: add the two C sources to whatever lists the ss sources. Mednafen has
# used autotools (Makefile.am) and meson (meson.build) across versions, so try to find
# a file that references ss/vdp1.cpp (or vdp1.cpp) and add ours beside it.
BUILD_CANDIDATES = [
    os.path.join("src", "ss", "Makefile.am"),   # current Mednafen ss (libss_a_SOURCES)
    os.path.join("src", "Makefile.am"),
    os.path.join("src", "meson.build"),
    os.path.join("src", "ss", "Makefile.inc"),
]
NEW_SOURCES = ["se_export.c", "se_mednafen_glue.c"]
# The vdp1 source token, keeping any path prefix (group 2) and quoting (group 1) the
# list uses. Matches `vdp1.cpp`, `ss/vdp1.cpp`, `'vdp1.cpp'` — but not `vdp1_line.cpp`.
VDP1_SRC_RE = re.compile(r'''(["']?)((?:\S+/)?vdp1\.cpp)\1''')


def process_build(root, do_write):
    for rel in BUILD_CANDIDATES:
        path = os.path.join(root, rel)
        if not os.path.isfile(path):
            continue
        text = open(path, encoding="utf-8", errors="surrogateescape").read()
        if "se_mednafen_glue.c" in text:
            return [f"{rel}:", "  ok (already)  se_export.c + se_mednafen_glue.c"]
        m = VDP1_SRC_RE.search(text)
        if not m:
            continue
        quote, ref = m.group(1), m.group(2)
        prefix = ref[: ref.rfind("vdp1.cpp")]   # "ss/", "", ...
        # Insert our two sources inline right before the vdp1 token, matching the list's
        # quoting/separators: meson wants `'x', 'y', `; make just space-separates. This
        # keeps the entry on its own line, valid for both `\`-continued and one-line lists.
        if quote:
            ins = "".join(f"{quote}{prefix}{s}{quote}, " for s in NEW_SOURCES)
        else:
            ins = "".join(f"{prefix}{s} " for s in NEW_SOURCES)
        new = text[: m.start()] + ins + text[m.start():]
        if do_write:
            open(path, "w", encoding="utf-8", errors="surrogateescape").write(new)
        return [f"{rel}:", f"  + inserted    {', '.join(NEW_SOURCES)}"]
    return ["build file:",
            "  ANCHOR MISS   add se_export.c + se_mednafen_glue.c to the ss build by hand"]


def revert(src_dir, root):
    fence_re = re.compile(re.escape(BEGIN) + r".*?" + re.escape(END) + r"\n?", re.DOTALL)
    edited = [os.path.join(src_dir, f) for f in
              ("vdp1.cpp", "vdp2.cpp", "ss.cpp", "sound.cpp", "scsp.h", "cdb.cpp", "smpc.cpp", "smpc.h")]
    edited.append(os.path.join(root, TITLE_FILE))   # window-title mark (SDL frontend)
    edited.append(os.path.join(root, INPUT_FILE))   # keyboard-map hook (SDL frontend)
    for path in edited:
        if os.path.isfile(path):
            t = open(path, encoding="utf-8", errors="surrogateescape").read()
            open(path, "w", encoding="utf-8", errors="surrogateescape").write(fence_re.sub("", t))
    for name in COMMON_FILES + [GLUE_FILE]:
        p = os.path.join(src_dir, name)
        if os.path.isfile(p):
            os.remove(p)
    for rel in BUILD_CANDIDATES:
        path = os.path.join(root, rel)
        if os.path.isfile(path):
            t = open(path, encoding="utf-8", errors="surrogateescape").read()
            for s in NEW_SOURCES:
                # Remove the inline token we inserted before vdp1 (quoted or not, with
                # any path prefix and a trailing comma/space).
                t = re.sub(r'''["']?(?:\S+/)?''' + re.escape(s) + r'''["']?,?[ \t]*''', "", t)
            open(path, "w", encoding="utf-8", errors="surrogateescape").write(t)
    print("Reverted: removed SE_EXPORT edits and copied files.")


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    flags = {a for a in sys.argv[1:] if a.startswith("--")}
    if not args:
        print(__doc__)
        sys.exit(1)
    root = args[0]
    src_dir = find_src(root)
    if not src_dir:
        print(f"error: no ss.cpp under {root} (expected <root>/src/ss).")
        sys.exit(2)
    # If the caller passed src/ss directly, recover the repo root for build files.
    if os.path.basename(src_dir) == "ss" and os.path.basename(os.path.dirname(src_dir)) == "src":
        root = os.path.dirname(os.path.dirname(src_dir))
    print(f"Mednafen ss src: {src_dir}")

    if "--revert" in flags:
        revert(src_dir, root)
        return

    do_write = "--check" not in flags
    with_pause = "--with-pause" in flags
    if not do_write:
        print("(--check: reporting only, no files changed)\n")

    notes = []
    if do_write:
        notes.append("copy:")
        notes += copy_sources(src_dir, do_write)
    for fname, block, key in APPEND_EDITS:
        notes += process_append_file(src_dir, fname, block, key, do_write)
    notes += process_smpc(src_dir, do_write)
    notes += process_ss(src_dir, do_write, with_pause)
    notes += process_vdp1_drawend(src_dir, do_write)
    notes += process_sound(src_dir, do_write)
    notes += process_cd(src_dir, do_write)
    notes += process_title(root, do_write)
    notes += process_input(root, do_write)
    notes += process_build(root, do_write)

    print("\n".join(notes))
    misses = [n for n in notes if "ANCHOR MISS" in n or "MISSING" in n]
    if misses:
        print("\nSome hooks need manual placement — see Integration/Mednafen/README.md.")
        sys.exit(3)
    print("\nDone. Build Mednafen as usual; Saturn Explorer can now connect (--live).")
    if not with_pause:
        print("(Pause/step gate not injected — pass --with-pause to add it.)")


if __name__ == "__main__":
    main()
