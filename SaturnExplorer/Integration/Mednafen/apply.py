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
vdp1.cpp / vdp2.cpp / ss.cpp (where those statics are visible — a re-opened namespace
block), copies the portable server (../Common/se_export.{c,h} + SeLiveProtocol.h) plus
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
COMMON_FILES = ["se_export.c", "se_export.h", "SeLiveProtocol.h"]
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
extern "C" void SsDbgVdp1Regs(unsigned short o[11]) {
   o[0]=TVMR; o[1]=FBCR; o[2]=PTMR; o[3]=EWDR; o[4]=EWLR; o[5]=EWRR;
   o[6]=0;    o[7]=EDSR; o[8]=LOPR; o[9]=0;    o[10]=0;   /* ENDR/COPR/MODR write-only/computed */
}
}}"""

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
extern "C" const unsigned short* SsDbgSoundRam(void) {
   /* SCSP sound RAM (512 KiB) for the v13 Sound RAM tab / music-swap prototype. Returning
      NULL ships an EMPTY sound-RAM block (the client just shows an empty tab); WRITES still
      work via SsDbgPokeByte at 0x25A00000. TODO(mednafen): to enable the READ view, return a
      pointer to Beetle-Saturn's SCSP RAM here (host-order uint16 words, like VRAM — the glue
      SwapU16ToBE's it to big-endian). The SCSP RAM lives in ss/scsp.cpp; move this accessor
      into that file next to the SCSP instance and return its 262144-word RAM buffer. */
   return (const unsigned short*)0;
}
extern "C" int SsDbgScspSlots(unsigned char* out) {
   /* Decoded SCSP voices (v14) for the Sound panel + per-voice Play/Export. Serialize the 32
      decoded slots into 'out' as 36-byte little-endian records (layout in SeLiveProtocol.h) and
      return the count (32). The LIVE fields (EnvLevel/EnvPhase/CurrentAddr) are what tell the
      panel which voices are actually sounding — they exist only in the decoded slot struct.
      STUB by default (returns 0 -> empty Sound panel). TODO(mednafen): confirm the global
      SS_SCSP instance symbol + SS_SCSP_Slot field spellings on your tree, then enable the
      template below (a small helper writes each field at its documented offset):

      // static void W32(unsigned char* p, unsigned int v){p[0]=v;p[1]=v>>8;p[2]=v>>16;p[3]=v>>24;}
      // extern SS_SCSP SCSP;                     // the real global SCSP instance
      // const SS_SCSP_Slot* sl = SCSP.Slots;     // 32 decoded voices
      // for (int i=0;i<32;++i){ unsigned char* r=out+i*36; const SS_SCSP_Slot& s=sl[i];
      //   int released = (s.EnvPhase==3) && (s.EnvLevel>=0x3FF);
      //   r[0]=s.KeyBit?1:0; r[1]=(s.KeyBit&&!released)?1:0; r[2]=(unsigned char)s.EnvPhase;
      //   r[3]=s.WF8Bit?1:0; r[4]=(unsigned char)s.LoopMode; r[5]=(unsigned char)(signed char)s.Octave;
      //   r[6]=(unsigned char)s.TotalLevel;
      //   r[7]=(unsigned char)s.DirectLevel; r[8]=(unsigned char)s.DirectPan;   // derive from DISDL/DIPAN or DirectVolume[2]
      //   r[9]=(unsigned char)s.EffectLevel; r[10]=(unsigned char)s.EffectPan;
      //   r[11]=(unsigned char)s.EnvRates[0]; r[12]=(unsigned char)s.EnvRates[1];
      //   r[13]=(unsigned char)s.EnvRates[2]; r[14]=(unsigned char)s.EnvRates[3]; r[15]=(unsigned char)s.DecayLevel;
      //   r[16]=s.EnvLevel&0xFF; r[17]=s.EnvLevel>>8; r[18]=s.FreqNum&0xFF; r[19]=s.FreqNum>>8;
      //   W32(r+20,s.StartAddr); W32(r+24,s.LoopStart); W32(r+28,s.LoopEnd); W32(r+32,s.CurrentAddr); }
      // return 32; */
   (void)out;
   return 0;
}
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

SMPC_INPUT_DECL = """\
/* Saturn Explorer controller injection. `buttons` is the protocol's SE_PAD_* mask;
   the SMPC implementation translates and overlays it on the host gamepad state. */
void SMPC_SetInjectedInput(unsigned port, uint32 buttons);
"""

SMPC_INPUT_STATE = """\
/* Saturn Explorer controller injection. The server thread writes these masks while
   the emulation thread consumes them, so keep the handoff atomic. Values use
   Mednafen's digital-pad input bit order (see input/gamepad.cpp): bit0 UP, 1 DOWN,
   2 LEFT, 3 RIGHT, 4 START, 5 A, 6 B, 7 C, 8 X, 9 Y, 10 Z, 11 L, 12 R. */
static std::atomic_uint_least16_t SeInjectedPad[2];

extern "C" void SeExportLog(const char* msg);   /* diagnostic log -> SE Log window (v11+) */

void SMPC_SetInjectedInput(unsigned port, uint32 buttons)
{
 if(port >= 2) return;
 const uint16 native = (uint16)((buttons & 0x000Fu) |       /* directions: bits 0..3 */
                                ((buttons & 0x0FF0u) << 1) | /* A..R: bits 5..12 */
                                ((buttons & 0x1000u) >> 8)); /* Start: bit 4 */
 SeInjectedPad[port].store(native, std::memory_order_relaxed);
 /* Diagnostic: what the SMPC translate produced. Fires on change (the client only sends
    INP on change), so it shows each button as it is pressed/released in SE's Log window. */
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

static void SeSMPCUpdateInput(unsigned vp, const int32 time_elapsed)
{
 uint8* data = VirtualPortsDPtr[vp];
 uint8 merged[10];
 if(vp < 2 && data)
 {
  const uint16 inj = SeInjectedPad[vp].load(std::memory_order_relaxed);
  if(inj && VirtualPorts[vp] == &PossibleDevices[vp].gamepad)
  {
   /* Digital Control Pad: 2-byte data buffer, digital bits at [0..1]. */
   const uint16 host = (uint16)(data[0] | ((uint16)data[1] << 8));
   const uint16 combined = (uint16)(host | inj);
   merged[0] = (uint8)combined;
   merged[1] = (uint8)(combined >> 8);
   data = merged;
   /* Diagnostic: what the pad actually receives after merging SE's injection with any
      host input. Logged only when it changes so the log isn't flooded every frame. */
   static uint16 sLastMerged[2] = { 0xFFFFu, 0xFFFFu };
   if(vp < 2 && combined != sLastMerged[vp])
   {
    char m[96];
    snprintf(m, sizeof(m), "pad merge: port=%u inj=0x%04X host=0x%04X -> pad=0x%04X",
             vp, (unsigned)inj, (unsigned)host, (unsigned)combined);
    SeExportLog(m);
    sLastMerged[vp] = combined;
   }
  }
  else if(inj && VirtualPorts[vp] == &PossibleDevices[vp].threedpad)
  {
   /* 3D Control Pad: 10-byte buffer (input/3dpad.cpp) — digital bits at [0..1] (bits
      0..10 share the gamepad layout), analog stick at [2..5], analog shoulders at
      [6..9]. OR the digital bits in, and drive L/R as full analog when injected so
      shoulder games respond. Mode + stick are left as the host provides. */
   memcpy(merged, data, 10);
   const uint16 dtmp = (uint16)((data[0] | ((uint16)data[1] << 8)) | (inj & 0x07FFu));
   merged[0] = (uint8)dtmp;
   merged[1] = (uint8)(dtmp >> 8);
   if(inj & (1u << 11)) { merged[6] = 0xFF; merged[7] = 0xFF; } /* L shoulder analog full */
   if(inj & (1u << 12)) { merged[8] = 0xFF; merged[9] = 0xFF; } /* R shoulder analog full */
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
    if do_write and text != original:
        open(path, "w", encoding="utf-8", errors="surrogateescape").write(text)
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
              ("vdp1.cpp", "vdp2.cpp", "ss.cpp", "smpc.cpp", "smpc.h")]
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
