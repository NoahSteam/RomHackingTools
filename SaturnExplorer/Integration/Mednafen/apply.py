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

import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SHARED_DIR = os.path.normpath(os.path.join(HERE, "..", "Common"))
# Copied verbatim from Common; the glue is copied from HERE with SE_MEDNAFEN_WIRED
# prepended so no per-file build flag is needed.
COMMON_FILES = ["se_export.c", "se_export.h", "SeLiveProtocol.h", "SeStateCodec.h", "SePerfCounters.h"]
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
/* The latch is not part of Mednafen's state, so it must not outlive the VRAM it copied: a
   state load or a reset replaces the timeline, and a stale latch would keep describing the
   abandoned one (command list, VRAM Map, reconstructed output) until the next draw-end. After
   this, the glue falls back to live VRAM exactly as on a fresh boot. */
extern "C" void SsDbgVdp1LatchInvalidate(void) { SsVdp1LatchValid = 0; }
extern "C" void SsDbgVdp1LatchDrawEnd(void) {
   memcpy(SsVdp1Latch, VRAM, sizeof SsVdp1Latch);
   SsVdp1LatchValid = 1;
}
extern "C" const unsigned short* SsDbgVdp1Latch(void) {
   return SsVdp1LatchValid ? SsVdp1Latch : (const unsigned short*)0;
}
/* A debugger poke into VDP1 VRAM ('off' within the 512 KiB, big-endian in the word) goes into the
   latch too. The snapshot shows the latch, so otherwise an edit made at a breakpoint or while
   stepping vanished from the view at the next capture -- though VRAM, and so the next draw, had
   it -- and stayed gone until the following draw-end. */
extern "C" void SsDbgVdp1LatchPokeByte(unsigned int off, unsigned char val) {
   if (SsVdp1LatchValid) ne16_wbo_be<uint8>(SsVdp1Latch, off & 0x7FFFF, val);
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
VDP1_DRAWEND_FWD = (
    'extern "C" void SsDbgVdp1LatchDrawEnd(void);  /* SE_VDP1_LATCH_FWD */\n'
    'extern "C" void SsDbgVdp1LatchInvalidate(void);\n'
)
# The latch must not survive a state load or a reset (see SsDbgVdp1LatchInvalidate). Anchored
# on the start of VDP1::Reset (both power-on and soft reset) and on the VDP1 section's
# MDFNSS_StateAction, which is what restores VRAM on a load.
VDP1_RESET_ANCHOR = r'(void Reset\(bool powering_up\)\s*\{\s*\n)'
VDP1_RESET_HOOK = (
    "  SsDbgVdp1LatchInvalidate();   /* SE_VDP1_LATCH_RESET */\n"
)
VDP1_LOAD_ANCHOR = r'(MDFNSS_StateAction\(sm, load, data_only, StateRegs, "VDP1"\);\s*\n)'
VDP1_LOAD_HOOK = (
    "  if(load) SsDbgVdp1LatchInvalidate();   /* SE_VDP1_LATCH_LOAD */\n"
)
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
   /* PC. The pipeline-stage PCs (PC_ID = decode, PC_IF = fetch) are maintained ONLY by
      the debug run loop: every write to them in sh7095.inc sits behind `if(DebugMode)`,
      and DebugMode is the Step<true> template argument that ss.cpp's dispatcher
      (rltab[..][DBG_NeedCPUHooks()]) selects solely while a breakpoint, tracepoint or CPU
      hook is installed. With nothing armed the fast loop never touches PC_ID, so it keeps
      whatever it last held — 0 from reset. Reading it unconditionally therefore reported
      PC 00000000 for every free-running snapshot, which is what put "sub_000000 /
      PC 00000000" in the client's Call Stack frame #0 and SH-2 register panel.
      GSREG_RPC is the raw fetch pointer, maintained in both loops; it runs two
      instructions (4 bytes) ahead of the decode stage, so PC_ID == RPC - 4. Prefer the
      authoritative PC_ID while the debug loop is actually running — a breakpoint halt or
      an instruction step, where being exact matters for the Assembly panel's highlighted
      row — and derive the PC from RPC otherwise. */
   o[22] = DBG_NeedCPUHooks() ? c.GetRegister(SH7095::GSREG_PC_ID, 0, 0)
                              : (c.GetRegister(SH7095::GSREG_RPC, 0, 0) - 4);
}
extern "C" void SsDbgVdp1LatchPokeByte(unsigned int off, unsigned char val);   /* vdp1.cpp */
extern "C" void SsDbgPokeByte(unsigned int addr, unsigned char val) {
   /* Route to Mednafen's own byte bus-write (used by the cheat engine): it does the
      writeability check + SH-2 cache invalidation and takes a Saturn bus address, so
      writing byte-by-byte preserves big-endian order — the conventional debugger poke,
      for any region (work RAM, VRAM, ...). If your fork lacks CheatMemWrite, point this
      at the equivalent bus/debug byte writer. Sound-RAM pokes (v13) also route here at
      the SCSP RAM bus base 0x25A00000 (see SeMdfnWriteSoundByte in the glue). */
   CheatMemWrite((unsigned int)addr, (unsigned char)val);
   /* VDP2 VRAM has a second copy: the renderer (vdp2_render.cpp, possibly on its own thread)
      keeps its own VRAM, fed only by VDP2REND_Write16_DB. CheatMemWrite writes the fast-map
      array alone, so the CPU and the snapshot saw the edit while the game kept drawing the
      old tiles. PokeVRAM rewrites the same byte (a no-op for the array) and forwards the
      word to the renderer; CheatMemWrite above has already updated the SH-2 caches. */
   const unsigned int a = addr & ((1U << 27) - 1);
   if (a >= 0x05E00000u && a < 0x05F00000u) VDP2::PokeVRAM(a, (uint8)val);
   /* VDP1 VRAM is shown from the draw-end latch (vdp1.cpp's SsDbgVdp1Latch), which this write
      does not reach on its own. */
   if (a >= 0x05C00000u && a < 0x05C80000u) SsDbgVdp1LatchPokeByte(a - 0x05C00000u, (unsigned char)val);
}
/* An SH-2's cycle counter (v26 profiler): the glue takes the difference between two instructions
   as the cost of the first. It restarts from near zero every frame (RebaseTS), and reads
   SS_EVENT_DISABLED_TS while the slave is switched off; the glue discards both jumps. */
extern "C" int SsDbgCpuTimestamp(int cpu) { return (int)CPU[cpu ? 1 : 0].timestamp; }
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
   halts if the SS debugger flagged a PC breakpoint at this instruction.

   BOTH SH-2s. Stock DBG_CPUHandler returns at its first line when which != DBG.ActiveCPU --
   the debugger UI's "current CPU", the master unless something changed it, and nothing in
   this integration does -- so the slave's breakpoints, tracepoints and call tracking never ran.
   process_debug patches DBG_CPUHandler to run the whole handler for the CPU that is executing
   whenever SeSsBothCpus is set (our callback is installed), by making that CPU the "active" one
   for the duration of the call (SeSsActiveCpuScope) and putting it in SeSsExecCpu. So the
   callback knows which CPU it is looking at from SeSsExecCpu, not from DBG.ActiveCPU, and
   user breakpoints stay shared: a PC breakpoint halts whichever core reaches it. */
static void SeSsBpHook(uint32 PC, bool bpoint);   /* fwd: SeSyncCpuHook installs it */
static int sSeBpActive = 0;      /* >=1 execution breakpoint installed */
static int sSeTraceActive = 0;   /* >=1 enabled tracepoint armed */
static int sSeStepActive = 0;    /* an instruction step is in progress */
static int sSeProfActive = 0;    /* the profiler is attributing SH-2 time to functions (v26) */
/* Set while >=1 data (read/write) watchpoint is installed. scu.inc's DMA_Write reads it to
   cheap-gate the SCU-DMA watchpoint check (SeSsDmaWatch); C linkage + a global so scu.inc,
   which is included before debug.inc where DBG lives, can test it without the debugger guts. */
extern "C" { int SeSsMemWatchArmed = 0; }
/* SeSsBothCpus: run the debugger handler for whichever SH-2 is executing (see above).
   SeSsExecCpu: the CPU DBG_CPUHandler is currently running for (0 master / 1 slave). */
extern "C" { int SeSsBothCpus = 0; int SeSsExecCpu = 0; }
/* Install/remove the per-instruction callback to match what is armed: continuous (every
   instruction) when tracepoints OR an instruction step are active so the hook sees every
   PC; non-continuous (fires only when the debugger finds a PC breakpoint) when only
   breakpoints exist; removed entirely when none is active so the fast run loop returns.
   Continuous still passes bpoint=true on breakpoint PCs, so it is a superset. */
/* Set while a halt holds the CPUs (the gate spin below): the mode is then being edited, not entered. */
static int sSeHalted = 0;
/* DBG_CPUHandler depth (see SeSsActiveCpuScope in debug.inc) and a re-arm it postponed. */
extern "C" { int SeSsInHandler = 0; }
static int sSeSyncPending = 0;
static void SeSyncCpuHook(void) {
   /* Never change the callback while DBG_CPUHandler is running: it has already decided to call it.
      The handler's scope guard re-runs this as it returns, before the next instruction. */
   if (SeSsInHandler > 0) { sSeSyncPending = 1; return; }
   sSeSyncPending = 0;
   /* Function profiling needs every instruction of both CPUs, exactly like a tracepoint. */
   const int every = sSeTraceActive || sSeStepActive || sSeProfActive;
   const int on = sSeBpActive || every;
   SeSsBothCpus = on;
   /* A held call/return is completed by the instructions that follow it; if the CPU is about to run
      without presenting them, it must be told (it is not told of an edit made while halted). */
   if (!sSeHalted) SeMednafenHookMode(every != 0);
   if (on)
      DBG_SetCPUCallback(SeSsBpHook, every != 0);
   else
      DBG_SetCPUCallback(0, false);
}
extern "C" void SeSsHandlerExit(void) { if (sSeSyncPending) SeSyncCpuHook(); }
static void SeSsBpHook(uint32 PC, bool bpoint) {
   /* The CPU this call is about -- passed explicitly (see the note above), never read back from
      DBG.ActiveCPU. */
   const int cpu = SeSsExecCpu;
   /* Per-instruction (continuous mode): drive tracepoints + the shadow call stack. This
      runs BEFORE the halt gate, so a tracepoint on the very PC a breakpoint also halts on
      still fires as the PC is reached. */
   SeMednafenTraceHook(cpu, (unsigned int)PC);
   /* Instruction step: count this instruction on the stepped CPU; halt when the budget
      is spent (SeExportInsnStepTick returns 1). The tick needs to know whether a repeat of the
      same PC is the branch-to-self retiring or the SH-2 stalled on the bus; only the glue can
      decode the instruction to tell. */
   int stepHalt = sSeStepActive
      ? SeExportInsnStepTick(cpu, (unsigned int)PC, SeMednafenSelfBranchTaken(cpu, (unsigned int)PC))
      : 0;
   if (bpoint || stepHalt) {
      if (stepHalt && !bpoint)
         SeExportNotifyStep(cpu, (unsigned int)PC);
      else
         SeExportNotifyStop(cpu, (unsigned int)PC);
      /* Publish a snapshot NOW, from inside the halt, so the client sees the registers and
         memory AT the breakpoint/step instead of the last completed frame's end state. The
         normal snapshot only runs at end-of-frame, which a mid-frame halt never reaches, so
         without this the SH-2 regs / RAM the debugger shows are stale (frozen a frame back)
         and stepping looks dead even though the PC is advancing. sPaused is already set, so
         SeExportSnapshot skips the rewind-ring capture (guarded on !sPaused). */
      sSeHalted = 1;   /* before the snapshot: it applies queued installs, whose SeSyncCpuHook must see a halt */
      SeMednafenFrameHook();
      while (!SeExportGateHalt()) { }   /* a load waits for the frame boundary; see SeGateDecide */
      sSeHalted = 0;
      /* Gate released: set the callback mode for what runs next — continuous iff an
         instruction step (IST) was just requested, else it reverts to the bp/tracepoint
         arming. The mode during the spin above is inert (no instructions execute), so a
         single sync point here is enough. */
      sSeStepActive = SeExportInsnStepBegin() ? 1 : 0;
      SeSyncCpuHook();
   }
}
/* SCU-DMA watchpoint check. Mednafen's ss RW breakpoints only see SH-2 *instruction* effective
   addresses (CheckRWBreakpoints in DBG_CPUHandler), so data the SCU DMAs into memory — how games
   normally fill VDP1/VDP2 VRAM — never trips a watchpoint, even though Yabause's bus-level
   watchpoints do. scu.inc's DMA_Write calls this once per transferred unit (only when
   SeSsMemWatchArmed, so a DMA with no watchpoint set pays a single branch). On a hit it halts
   exactly like an SH-2 watchpoint: reuse DBG_CheckWriteBP/ReadBP to range-match, then notify +
   publish the halted snapshot + spin in the frame gate. A DMA write has no instruction of its
   own, so we report the master PC (where execution was when the DMA ran) as the halt location. */
extern "C" void SeSsDmaWatch(unsigned int A, unsigned int len, int isWrite) {
   /* While a single-step is in progress, don't let a DMA write re-halt: the watched cell is
      often written as several sub-word DMA writes, and this routine may itself re-run the DMA
      every frame — either would re-halt before the stepped instruction retires and pin the PC.
      The watchpoint resumes catching DMA writes as soon as the step completes or the user
      continues (sSeStepActive is cleared then). */
   if (sSeStepActive) return;
   DBG.FoundBPoint = false;
   if (isWrite) DBG_CheckWriteBP(len, A);
   else         DBG_CheckReadBP(len, A);
   if (!DBG.FoundBPoint) return;
   DBG.FoundBPoint = false;
   /* A halt between instructions: the instruction at this PC has not run (NotifyDmaStop). */
   SeExportNotifyDmaStop(0, (unsigned int)CPU[0].GetRegister(SH7095::GSREG_PC_ID, NULL, 0));
   sSeHalted = 1;   /* before the snapshot: it applies queued installs, whose SeSyncCpuHook must see a halt */
   SeMednafenFrameHook();          /* publish the halted state (regs/RAM at the DMA write) */
   while (!SeExportGateHalt()) { }
   sSeHalted = 0;
   /* Hand off to the CPU step machinery if a single-step (IST) was requested from this DMA
      halt, exactly like SeSsBpHook does after its gate: arm the per-instruction hook so the
      next retired instruction counts down the step budget and halts. Without this a Step from a
      DMA-watchpoint halt just resumes (the halt is outside the instruction hook), so it never
      single-steps and, once the watchpoint stops re-hitting, runs away. The step counter is
      retire-based (see SeExportInsnStepTick), so the CPU being bus-stalled while the DMA drains
      doesn't eat the budget, and the halt PC was seeded by SeExportNotifyDmaStop as an
      instruction that has not run yet. */
   sSeStepActive = SeExportInsnStepBegin() ? 1 : 0;
   SeSyncCpuHook();
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
   SeSsMemWatchArmed = 1;   /* arm the SCU-DMA watchpoint check (see SeSsDmaWatch) */
   SeSyncCpuHook();
}
extern "C" void SsDbgClearBps(void) {
   DBG_FlushBreakPoints(BPOINT_PC);
   DBG_FlushBreakPoints(BPOINT_READ);
   DBG_FlushBreakPoints(BPOINT_WRITE);
   sSeBpActive = 0;
   SeSsMemWatchArmed = 0;   /* no data watchpoints left -> DMA writes skip the check */
   SeSyncCpuHook();   /* keeps the continuous hook if tracepoints are still armed */
}
/* Arm/disarm per-instruction tracepoint scanning. Called by the glue whenever the
   tracepoint set changes (active = at least one enabled tracepoint). */
extern "C" void SsDbgSetTraceActive(int active) {
   sSeTraceActive = active ? 1 : 0;
   SeSyncCpuHook();
}
/* Whether the CPU hooks above are real: 1 in a --enable-debugger (WANT_DEBUGGER) build. The glue
   advertises breakpoints, watchpoints, instruction steps and tracepoints from it, so a client does
   not offer controls that the no-op stubs below would silently ignore. */
extern "C" int SsDbgHasDebugger(void) { return 1; }
/* Arm/disarm per-instruction function profiling (v26): the glue samples in SeMednafenTraceHook,
   which only runs while the callback is continuous. */
extern "C" void SsDbgSetProfileActive(int active) {
   sSeProfActive = active ? 1 : 0;
   SeSyncCpuHook();
}
#else
extern "C" int SsDbgHasDebugger(void) { return 0; }
extern "C" void SsDbgSetProfileActive(int active) { (void)active; }
extern "C" { int SeSsInHandler = 0; }
extern "C" void SeSsHandlerExit(void) {}
extern "C" void SsDbgAddExecBp(int cpu, unsigned int addr) { (void)cpu; (void)addr; }
extern "C" void SsDbgAddMemBp(int cpu, unsigned int addr, unsigned int size, unsigned int kind) { (void)cpu; (void)addr; (void)size; (void)kind; }
extern "C" void SsDbgClearBps(void) {}
extern "C" void SsDbgSetTraceActive(int active) { (void)active; }
extern "C" { int SeSsBothCpus = 0; int SeSsExecCpu = 0; }
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
   // Sounding = still producing output. Key-off only starts the release, which keeps playing
   // until the envelope is fully attenuated, so a keyed-off voice with an audible tail is
   // active; key-on (r[0]) is reported separately.
   r[1]  = ((s.KeyBit || s.EnvPhase == ENV_PHASE_RELEASE) && !released) ? 1 : 0;
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
# Pokes into the two arrays the emulator's own bus writer (CheatMemWrite) cannot reach (v23). That
# writer goes through SH7095_FastMap, which only holds the BIOS, work RAM, VDP1/VDP2 VRAM and sound
# RAM -- see SS_SetPhysMemMap callers in ss.cpp, vdp1.cpp, vdp2.cpp and sound.cpp; the commented-out
# VDP1 FB mapping and the absent CRAM one are why a CRAM or frame-buffer poke was accepted and
# dropped. These write the SAME arrays the snapshot accessors expose, byte for byte as a read sees
# them, so the next frame shows the edit.
#   * CRAM: 'off' is a byte offset into the CPU's CRAM window (0x05F00000), the layout the client
#     shows. In RGB888 mode Mednafen stores bus word cri at CRAM[(cri >> 1) | (cri & 1) << 10] (the
#     high halves of all entries, then the low halves), so the raw array is not that layout;
#     SsDbgCramWire exports the bus view and the poke reads-modifies-writes the same bus word, so a
#     poke and the next capture name the same byte in every mode. The write goes through
#     VDP2::Write16_DB, the bus path, so VDP2REND's own copy of CRAM is updated too -- writing the
#     array alone would leave the renderer on the old colours.
#   * VDP1 FB: the DISPLAYED bank, FB[!FBDrawWhich], exactly what SsDbgVdp1Fb reads (the CPU-visible
#     window is the draw bank, so a bus write would change a buffer the snapshot never shows). 'off'
#     is big-endian within each word, matching the glue's byte swap of the capture.
VDP2_POKE_ACCESSORS = """\
/* Saturn Explorer v23: CRAM as the CPU's bus window sees it, for capture and poke alike. */
namespace MDFN_IEN_SS { namespace VDP2 {
static unsigned SeCramRawIndex(unsigned cri) {
   cri &= 0x7FF;
   return (CRAM_Mode == CRAM_MODE_RGB555_1024 || CRAM_Mode == CRAM_MODE_RGB555_2048)
          ? cri : (((cri >> 1) & 0x3FF) | ((cri & 1) << 10));
}
/* Fills 'out' (4096 bytes) in the little-endian order the client's CRAM normalization undoes: RAMCTL
   CRMD 2 reverses each 4-byte entry, every other mode each 2-byte word. That is the Yabause
   wire shape, built here from bus words rather than from Mednafen's raw array. */
extern "C" void SsDbgCramWire(unsigned char* out) {
   const bool rgb888 = CRAM_Mode == CRAM_MODE_RGB888_1024;
   for (unsigned cri = 0; cri < 2048; ++cri) {
      const uint16 v = CRAM[SeCramRawIndex(cri)];
      const unsigned at = rgb888 ? ((cri & ~1u) * 2 + ((cri & 1) ? 0 : 2)) : cri * 2;
      out[at] = (unsigned char)v;
      out[at + 1] = (unsigned char)(v >> 8);
   }
}
extern "C" void SsDbgPokeCramByte(unsigned int off, unsigned char val) {
   const unsigned cri = (off >> 1) & 0x7FF;
   const uint16 cur = CRAM[SeCramRawIndex(cri)];
   const uint16 nv = (off & 1) ? (uint16)((cur & 0xFF00) | val)
                               : (uint16)((cur & 0x00FF) | ((uint16)val << 8));
   Write16_DB(0x100000 | (cri << 1), nv);
}
}}"""

VDP1_POKE_ACCESSORS = """\
/* Saturn Explorer v23: byte poke into the displayed VDP1 frame-buffer bank (see SsDbgVdp1Fb). */
namespace MDFN_IEN_SS { namespace VDP1 {
extern "C" void SsDbgPokeVdp1FbByte(unsigned int off, unsigned char val) {
   ne16_wbo_be<uint8>(FB[!FBDrawWhich], off & 0x3FFFF, val);
}
}}"""

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
# consume a full state image. install.py defines SE_MDFN_REWIND=1, so the real MDFNSS path below
# is what builds; the #else stub is the fallback for a fork whose savestate-to-memory API differs
# (MDFNSS_SaveSM / MDFNSS_LoadSM / MemoryStream moving namespace, or the MemoryStream constructor
# changing meaning). With the stub, SsDbgSaveState returns 0, the ring stays empty and rewind /
# save state silently do nothing — everything else (protocol, client, UI) degrades gracefully.
# See Integration/Mednafen/README.md, "Rewind", before changing the pinned fork.
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
 /* One stream, reused for the life of the process. A fresh MemoryStream starts with no buffer
    and grows to the full state size through repeated realloc() (grow_if_necessary ->
    round_up_pow2), then frees the lot -- and this runs EVERY frame while a client is attached,
    so the rewind ring was paying a multi-MB allocate-grow-free cycle per frame. truncate(0)
    only assigns data_buffer_size; it leaves data_buffer_alloced alone, so after the first save
    the allocation is already large enough and grow_if_necessary never reallocs again.
    Reused safely because only the emulate thread calls this (se_export's SeStateCapture), and
    C++11 guarantees the static is initialized once. */
 try { /* Inside the try: the default constructor reallocs 64 bytes and throws MDFN_Error on
          failure, and this is an extern "C" entry point called from se_export.c -- letting that
          escape would unwind through a C frame. A function-local static still keeps its buffer
          between calls from here, and a throw here just leaves it uninitialized for a retry. */
       static Mednafen::MemoryStream ms;
       ms.truncate(0);                 /* keep the allocation, drop the contents */
       ms.seek(0, SEEK_SET);
       Mednafen::MDFNSS_SaveSM(&ms, true);   /* data_only: no preview */
       uint64 sz = ms.size();
       if(!buf) return (size_t)sz;
       if((uint64)cap < sz) return 0;
       memcpy(buf, ms.map(), (size_t)sz);
       return (size_t)sz;
 } catch(...) { return 0; } }
extern "C" int SsDbgLoadState(const unsigned char* buf, size_t len) {
 if(!buf || !len) return -1;
 /* alloc_hint_is_size must be non-zero, or the stream's *size* stays 0 (the hint only
    reserves capacity) and MDFNSS_LoadSM reads an empty stream. -1 sets the size and skips
    the zero-fill, which the memcpy below overwrites anyway. */
 try { Mednafen::MemoryStream ms(len, -1); memcpy(ms.map(), buf, len); ms.rewind();
       Mednafen::MDFNSS_LoadSM(&ms, true); return 0; } catch(...) { return -1; } }
#else
extern "C" size_t SsDbgSaveState(unsigned char* buf, size_t cap) { (void)buf; (void)cap; return 0; }
extern "C" int    SsDbgLoadState(const unsigned char* buf, size_t len) { (void)buf; (void)len; return -1; }
#endif"""

# Emulator-native save slots (v17; saving them v25). Lets Saturn Explorer list, load and save Mednafen's OWN numbered
# save slots -- the ones F5/F7 use -- which it cannot find itself: the path depends on the base
# directory, the filesys.path_state setting and an MD5 of the disc. MDFN_MakeFName builds it,
# and MDFNI_LoadState is given that path explicitly rather than going through MDFNI_SelectState,
# so loading from Saturn Explorer does not move the slot the user has selected in Mednafen.
#
# Unlike SAVESTATE_ACCESSORS there is no stub fallback here: these are ordinary public entry
# points, and if a fork changes them the result is a compile error, which is the failure mode
# you want. See README.md, "Rewind", for the one that fails silently.
EMU_SLOT_ACCESSORS = """\
/* Saturn Explorer: the emulator's own save slots (v17). */
#include <mednafen/general.h>
#include <mednafen/state-driver.h>
#include <sys/stat.h>
extern "C" int SsDbgEmuSlotInfo(unsigned slot, unsigned long long* mtime) {
 try { const std::string p = Mednafen::MDFN_MakeFName(Mednafen::MDFNMKF_STATE, (int)slot, NULL);
       struct stat st;
       if(::stat(p.c_str(), &st) != 0) return 0;
       if(mtime) *mtime = (unsigned long long)st.st_mtime;
       return 1;
 } catch(...) { return 0; } }
extern "C" int SsDbgEmuLoadSlot(unsigned slot) {
 try { const std::string p = Mednafen::MDFN_MakeFName(Mednafen::MDFNMKF_STATE, (int)slot, NULL);
       return Mednafen::MDFNI_LoadState(p.c_str(), NULL) ? 0 : -1;
 } catch(...) { return -1; } }
/* v25: write the slot F5 would, by path for the same reason as the load. No preview image: the
   frame's surface is not reachable from the frame boundary, and MDFNSS_SaveSM accepts none. */
extern "C" int SsDbgEmuSaveSlot(unsigned slot) {
 try { const std::string p = Mednafen::MDFN_MakeFName(Mednafen::MDFNMKF_STATE, (int)slot, NULL);
       return Mednafen::MDFNI_SaveState(p.c_str(), NULL, NULL, NULL, NULL) ? 0 : -1;
 } catch(...) { return -1; } }"""

SMPC_INPUT_DECL = """\
/* Saturn Explorer controller injection. `buttons` is the protocol's SE_PAD_* mask;
   the SMPC implementation translates and overlays it on the host gamepad state. */
void SMPC_SetInjectedInput(unsigned port, uint32 buttons);
"""

SMPC_INPUT_STATE = """\
/* Saturn Explorer controller injection. The server thread writes these masks while
   the emulation thread consumes them, so keep the handoff atomic. The stored value is
   the protocol's SE_PAD_* mask, NOT a data-buffer layout: the Digital Control Pad and the
   3D Control Pad lay their input buffers out differently, and the device on a port can be
   changed at runtime, so the translation happens where the injection is merged
   (SeSMPCUpdateInput), once the device is known.

   Both layouts are the ENTRY POSITION in the device's IDII (input/gamepad.cpp,
   input/3dpad.cpp) -- NOT the number in each IDIIS_Button(...) third argument, which is
   only the config-prompt order:
     Digital pad: 0 Z, 1 Y, 2 X, 3 R, 4 UP, 5 DOWN, 6 LEFT, 7 RIGHT,
                  8 B, 9 C, 10 A, 11 START, 12-14 padding, 15 L
     3D pad:      0 UP, 1 DOWN, 2 LEFT, 3 RIGHT, 4 B, 5 C, 6 A, 7 START,
                  8 Z, 9 Y, 10 X, 11 padding, 12 MODE; then 16-bit analog values at
                  byte 2 stick X, 4 stick Y, 6 right shoulder, 8 left shoulder
   (Digital layout confirmed against the emulator's own host input: pressing Right in
   Mednafen's window sets 0x0080 = bit 7.) */
static std::atomic_uint_least16_t SeInjectedPad[2];

extern "C" void SeExportLog(const char* msg);   /* diagnostic log -> SE Log window (v11+) */

/* SE_PAD_* -> data-buffer bit, one row per button. Spelled out as tables rather than shift
   arithmetic: the layouts interleave face buttons and directions, so there is no clean
   shift, and a wrong bit silently presses a different button (START was once mapped onto
   bit 4, which is UP on the digital pad -- and bit 4 is B on the 3D pad). A negative bit
   means the device has no digital bit for that button (the 3D pad's L/R are analog). */
struct SePadBit { uint16 se; int8 gamepad; int8 threedpad; };
static const SePadBit SePadBits[13] = {
 { 0x0001u,  4,  0 },   /* UP    */  { 0x0002u,  5,  1 },   /* DOWN  */
 { 0x0004u,  6,  2 },   /* LEFT  */  { 0x0008u,  7,  3 },   /* RIGHT */
 { 0x0010u, 10,  6 },   /* A     */  { 0x0020u,  8,  4 },   /* B     */
 { 0x0040u,  9,  5 },   /* C     */  { 0x0080u,  2, 10 },   /* X     */
 { 0x0100u,  1,  9 },   /* Y     */  { 0x0200u,  0,  8 },   /* Z     */
 { 0x0400u, 15, -1 },   /* L     */  { 0x0800u,  3, -1 },   /* R     */
 { 0x1000u, 11,  7 },   /* START */
};

static uint16 SeNativePadBits(uint16 se, bool threedpad)
{
 uint16 native = 0;
 for(unsigned i = 0; i < 13; i++)
 {
  const int bit = threedpad ? SePadBits[i].threedpad : SePadBits[i].gamepad;
  if(bit >= 0 && (se & SePadBits[i].se)) native |= (uint16)(1u << bit);
 }
 return native;
}

void SMPC_SetInjectedInput(unsigned port, uint32 buttons)
{
 if(port >= 2) return;
 const uint16 se = (uint16)(buttons & 0x1FFFu);
 const uint16 prev = SeInjectedPad[port].exchange(se, std::memory_order_relaxed);
 /* Diagnostic: only on change -- the LiveDriver poll thread re-sends INP every cycle
    while a button is held (to cover a non-latching glue), so logging every call would
    flood SE's Log window at poll rate. */
 if(se != prev)
 {
  char m[64];
  snprintf(m, sizeof(m), "SMPC inject: port=%u se=0x%04X", port, (unsigned)se);
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
  const uint16 se = SeInjectedPad[vp].load(std::memory_order_relaxed);
  const bool ispad = (VirtualPorts[vp] == &PossibleDevices[vp].gamepad);
  const bool is3d  = (VirtualPorts[vp] == &PossibleDevices[vp].threedpad);
  /* The injection in the device's own data-buffer layout (0 for any other device). */
  const uint16 inj = (ispad || is3d) ? SeNativePadBits(se, is3d) : (uint16)0;
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
   /* Data-buffer bit order = IDII entry order of the port's device (input/gamepad.cpp or
      input/3dpad.cpp); see the note on SMPC_SetInjectedInput. Padding bits are never
      named. */
   static const char* const bn_pad[16] =
    { "Z","Y","X","R","UP","DOWN","LEFT","RIGHT","B","C","A","START","-","-","-","L" };
   static const char* const bn_3d[16] =
    { "UP","DOWN","LEFT","RIGHT","B","C","A","START","Z","Y","X","-","MODE","-","-","-" };
   const char* const* const bn = is3d ? bn_3d : bn_pad;
   /* SsDbgQueryKeyMap reports in SE_PAD_* order (up,down,left,right,a,b,c,x,y,z,ls,rs,
      start); map each of those slots to the data-buffer bit above. */
   int km2data[13];
   for(unsigned k = 0; k < 13; k++)
    km2data[k] = is3d ? SePadBits[k].threedpad : SePadBits[k].gamepad;
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
  if(se != sLastInj[vp])
  {
   const uint16 h = hostBits;
   char m[128];
   snprintf(m, sizeof(m), "pad merge: port=%u se=0x%04X native=0x%04X host=0x%04X -> pad=0x%04X (%s)",
            vp, (unsigned)se, (unsigned)inj, (unsigned)h, (unsigned)(h | inj),
            ispad ? "gamepad" : (is3d ? "3D pad" : "NOT A PAD - injection ignored"));
   SeExportLog(m);
   sLastInj[vp] = se;
  }
  if(se && ispad)
  {
   /* Digital Control Pad: 2-byte data buffer, digital bits at [0..1]. */
   const uint16 host = (uint16)(data[0] | ((uint16)data[1] << 8));
   const uint16 combined = (uint16)(host | inj);
   merged[0] = (uint8)combined;
   merged[1] = (uint8)(combined >> 8);
   data = merged;
  }
  else if(se && is3d)
  {
   /* 3D Control Pad: 10-byte buffer (input/3dpad.cpp) -- digital bits at [0..1] (UP, DOWN,
      LEFT, RIGHT, B, C, A, START, Z, Y, X; its layout is NOT the digital pad's), mode in
      bit 12, then 16-bit little-endian analog values: stick X [2..3], stick Y [4..5],
      RIGHT shoulder [6..7], LEFT shoulder [8..9]. OR the digital bits in, and drive L/R
      as full analog when injected so shoulder games respond (the pad derives the
      digital L/R bits from those). Mode + stick are left as the host provides. */
   memcpy(merged, data, 10);
   const uint16 dtmp = (uint16)((data[0] | ((uint16)data[1] << 8)) | inj);
   merged[0] = (uint8)dtmp;
   merged[1] = (uint8)(dtmp >> 8);
   if(se & 0x0800u) { merged[6] = 0xFF; merged[7] = 0xFF; } /* R shoulder analog full */
   if(se & 0x0400u) { merged[8] = 0xFF; merged[9] = 0xFF; } /* L shoulder analog full */
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
    ("vdp1.cpp", VDP1_POKE_ACCESSORS, "SsDbgPokeVdp1FbByte"),
    ("vdp2.cpp", VDP2_POKE_ACCESSORS, "SsDbgPokeCramByte"),
]

# ---- Profiler counters (v26). SeSsPerfC is one array of per-frame totals, defined in the glue and
#      indexed by SePerfCounters.h (copied into the tree with the other shared files), so the
#      emulator, the wire and the client agree on its order by construction. The increments below
#      are unconditional: each sits on a path that already does far more work than one add (a bus
#      access, a DMA start, a sleeping CPU's idle step), and a flag test would cost what it saves.
#      Every hook is an INSERTION after an anchor, never a replacement, so --revert restores the
#      file exactly by deleting the fenced blocks. ----
PERF_DECL = (
    "#include \"SePerfCounters.h\"\n"
    "extern \"C\" unsigned long long SeSsPerfC[];\n"
    "extern \"C\" unsigned int SeSsCacheMode;   /* SE_PERF_CACHE_*, set where the emulator picks its mode */\n"
)
# Which counter an external-bus address belongs to (F8); see the comment inside the helper.
PERF_BUS_HELPER = """\
/* Which counter pair an SH-2 external-bus address belongs to, by its top 11 of 27 bits (64 KiB
   steps: every boundary of the SCU's own decode, scu.inc SCU_FromSH2_BusRW_DB, and of ss.cpp's CS0
   decode, falls on one). The value is the index of the pair's READ counter; the write counter
   follows it. Filled once by a static constructor. The windows are the SCU decoder's, not the
   address map's round numbers:
     A-bus   0x02000000-0x058FFFFF  (CS0, CS1, the dummy area 0x05000000-0x057FFFFF, CS2/CD)
     B-bus   0x05A00000-0x05FBFFFF  (SCSP, VDP1 0x05C00000-0x05D7FFFF, VDP2 0x05E00000-0x05FBFFFF)
     SCU     0x05FE0000-0x05FEFFFF
   Everything else between 0x02000000 and 0x05FFFFFF is a hole the SCU answers with "unknown
   access" and is counted as unmapped, not as the device next to it. */
static unsigned short SeSsBusSlot[2048];
static struct SeSsBusDevInit {
   SeSsBusDevInit() {
      for (unsigned i = 0; i < 2048; ++i) {
         const unsigned A = i << 16;
         unsigned short s;
         if      (A < 0x00100000u) s = SE_PERF_BUS_ACCESS + SE_PERF_DEV_BIOS * 2;
         else if (A < 0x00180000u) s = SE_PERF_BUS_ACCESS + SE_PERF_DEV_SMPC * 2;
         else if (A < 0x00200000u) s = SE_PERF_BUS_ACCESS + SE_PERF_DEV_BACKUP * 2;
         else if (A < 0x00400000u) s = SE_PERF_BUS_ACCESS + SE_PERF_DEV_WRAM_L * 2;
         else if (A < 0x02000000u) s = SE_PERF_BUS_ACCESS + SE_PERF_DEV_CS0_OTHER * 2;
         else if (A < 0x04000000u) s = SE_PERF_BUS_ACCESS + SE_PERF_DEV_ABUS_CS0 * 2;
         else if (A < 0x05000000u) s = SE_PERF_BUS_ACCESS + SE_PERF_DEV_ABUS_CS1 * 2;
         else if (A < 0x05800000u) s = SE_PERF_BUS_ACCESS_X + SE_PERF_XDEV_ABUS_DUMMY * 2;
         else if (A < 0x05900000u) s = SE_PERF_BUS_ACCESS + SE_PERF_DEV_ABUS_CD * 2;
         else if (A < 0x05A00000u) s = SE_PERF_BUS_ACCESS_X + SE_PERF_XDEV_UNMAPPED * 2;
         else if (A < 0x05C00000u) s = SE_PERF_BUS_ACCESS + SE_PERF_DEV_SCSP * 2;
         else if (A < 0x05D80000u) s = SE_PERF_BUS_ACCESS + SE_PERF_DEV_VDP1 * 2;
         else if (A < 0x05E00000u) s = SE_PERF_BUS_ACCESS_X + SE_PERF_XDEV_UNMAPPED * 2;
         else if (A < 0x05FC0000u) s = SE_PERF_BUS_ACCESS + SE_PERF_DEV_VDP2 * 2;
         else if (A < 0x05FE0000u) s = SE_PERF_BUS_ACCESS_X + SE_PERF_XDEV_UNMAPPED * 2;
         else if (A < 0x05FF0000u) s = SE_PERF_BUS_ACCESS + SE_PERF_DEV_SCU * 2;
         else if (A < 0x06000000u) s = SE_PERF_BUS_ACCESS_X + SE_PERF_XDEV_UNMAPPED * 2;
         else                      s = SE_PERF_BUS_ACCESS + SE_PERF_DEV_WRAM_H * 2;
         SeSsBusSlot[i] = s;
      }
   }
} sSeSsBusDevInit;
/* One request to the SH-2's bus state controller (BSC_BusRead / BSC_BusWrite in sh7095.inc: the two
   places every access that really reaches the bus goes through). The unit is the request, not the
   bus cycle: a 32-bit access to a 16-bit CS0 region is split inside and is still ONE request; a
   cache line fill is four (one first-word read and three burst reads); a cache hit and an
   instruction fetch the fast map serves are none. Cache emulation therefore changes the counts,
   which is why the mode travels with the sample (SeSsCacheMode). */
static inline void SeSsPerfBus(unsigned int A, int isWrite) {
   ++SeSsPerfC[SeSsBusSlot[(A >> 16) & 2047] + isWrite];
}
"""

SH2_BUSWRITE_ANCHOR = r'(void INLINE SH7095::BSC_BusWrite\(uint32 A, T V, const bool BurstHax, int32\* SH2DMAHax\)\s*\{[ \t]*\n)'
SH2_BUSWRITE_HOOK = " SeSsPerfBus(A, 1);   /* SE_PERF_BUS_WRITE */\n"
SH2_BUSREAD_ANCHOR = r'(T INLINE SH7095::BSC_BusRead\(uint32 A, const bool BurstHax, int32\* SH2DMAHax\)\s*\{[ \t]*\n)'
SH2_BUSREAD_HOOK = " SeSsPerfBus(A, 0);   /* SE_PERF_BUS_READ */\n"

# sh7095_ops.inc is compiled inside Step<which, ...> and inside the slave's resumable copy
# (sh7095s_rsu.inc, which defines `which` as an enum), so `which` names the CPU in both.
# A sleeping CPU re-executes its SLEEP pseudo-instruction every 3 cycles until an interrupt is
# pending; standby and a DMA-burst / external-halt stall idle the same way in steps of 7.
SH2_SLEEP_ANCHOR = r'(if\(MDFN_LIKELY\(!EPending\)\)\s*\{\s*\n\s*timestamp \+= 3;[ \t]*\n)'
SH2_SLEEP_HOOK = "\t SeSsPerfC[SE_PERF_SH2_SLEEP + which] += 3;   /* SE_PERF_SLEEP_HOOK */\n"
SH2_STANDBY_ANCHOR = r'(if\(MDFN_LIKELY\(Standby\)\)\s*\{\s*\n\s*timestamp \+= 7;[ \t]*\n)'
SH2_STANDBY_HOOK = "\t SeSsPerfC[SE_PERF_SH2_SLEEP + which] += 7;   /* SE_PERF_STANDBY_HOOK */\n"
SH2_STALL_ANCHOR = r'(if\(MDFN_LIKELY\(DMA_InBurst\(\) \|\| ExtHalt\)\)\s*\{\s*\n\s*timestamp \+= 7;[ \t]*\n)'
SH2_STALL_HOOK = "\t SeSsPerfC[SE_PERF_SH2_STALL + which] += 7;   /* SE_PERF_STALL_HOOK */\n"

# scu.inc: one count per transfer started (an indirect DMA starts one per table entry), with the
# byte count it was started with -- on the cold path, so the per-unit copy loop is untouched.
SCU_DMA_PERF_ANCHOR = r'(d->ReadFunc = rftab\[rb\];[ \t]*\n)'
SCU_DMA_PERF_HOOK = (
    " /* Saturn Explorer profiler: SCU DMA traffic by level and by bus route. */\n"
    " ++SeSsPerfC[SE_PERF_DMA_TRANSFERS + (d - DMALevel)];\n"
    " SeSsPerfC[SE_PERF_DMA_LEVEL_BYTES + (d - DMALevel)] += bc;\n"
    " SeSsPerfC[SE_PERF_DMA_ROUTE_BYTES + rb * 3 + wb] += bc;\n"
)
SCU_DSP_PERF_ANCHOR = r'(\(\(void \(\*\)\(void\)\)\(DSP_INSTR_BASE_UIPT[^\n]*\n\s*DSP\.CycleCounter -= 2;[ \t]*\n)'
SCU_DSP_PERF_HOOK = "  SeSsPerfC[SE_PERF_DSP_CYCLES] += 2;   /* Saturn Explorer profiler */\n"

# vdp1.cpp. Update() advances VDP1 by the cycles since it last ran; while a draw is in progress
# those are drawing cycles.
VDP1_PERF_FWD = "/* SE_VDP1_PERF_FWD */\n" + PERF_DECL
# Index 6 (elapsed VDP1 drawing, v26) is retired: Update() no longer adds the elapsed interval
# anywhere; the VDP1 figure is the command-work counter below.
# F4 (v27): VDP1 command work, counted where the command budget is DEBITED (VDP1_EAT_CLOCKS), not
# as elapsed time. The original macro is
#     CycleCounter -= (n);  case __COUNTER__:  if(CycleCounter <= 0) { ...; goto Breakout; }
# and a resumed draw re-enters at the `case` AFTER the debit, so the debit runs once per command phase
# however many Update() calls the phase spans. We re-define the macro right after the original (an
# insertion; revert deletes it and the original is back) with the debit routed through a function so
# `n` -- which at one use is a call that draws -- is still evaluated exactly once. The counter is
# bumped inside that function, i.e. before the `case` label. A local would be wrong: the label sits
# inside the macro's block, and jumping past an initialised local into it is ill-formed.
VDP1_EAT_ANCHOR = (r'(#define VDP1_EAT_CLOCKS\(n\)[ \t]*\\\n'
                   r'[ \t]*\{[ \t]*\\\n'
                   r'[ \t]*CycleCounter -= \(n\);[ \t]*\\\n'
                   r'[ \t]*case __COUNTER__:[ \t]*\\\n'
                   r'[ \t]*if\(CycleCounter <= 0\)[ \t]*\\\n'
                   r'[ \t]*\{[ \t]*\\\n'
                   r'[ \t]*CommandPhase = __COUNTER__ - CommandPhaseBias - 1;[ \t]*\\\n'
                   r'[ \t]*goto Breakout;[ \t]*\\\n'
                   r'[ \t]*\}[ \t]*\\\n'
                   r'[ \t]*\}[ \t]*\\\n[ \t]*\n)')
VDP1_EAT_HOOK = (
    "/* Saturn Explorer profiler (F4): the same macro, with the debit counted as command work. */\n"
    "static INLINE int32 SeSsVdp1Debit(int32 n) { SeSsPerfC[SE_PERF_VDP1_WORK_CYCLES] += (unsigned long long)(n > 0 ? n : 0); return n; }\n"
    "#undef VDP1_EAT_CLOCKS\n"
    "#define VDP1_EAT_CLOCKS(n)\t\t\t\t\t\t\t\t\\\n"
    "\t\t{\t\t\t\t\t\t\t\t\\\n"
    "\t\t CycleCounter -= SeSsVdp1Debit(n);\t\t\t\t\t\\\n"
    "\t\t case __COUNTER__:\t\t\t\t\t\t\t\\\n"
    "\t\t if(CycleCounter <= 0)\t\t\t\t\t\t\\\n"
    "\t\t {\t\t\t\t\t\t\t\t\\\n"
    "\t\t  CommandPhase = __COUNTER__ - CommandPhaseBias - 1;\t\t\t\t\\\n"
    "\t\t  goto Breakout;\t\t\t\t\t\t\\\n"
    "\t\t }\t\t\t\t\t\t\t\t\\\n"
    "\t\t}\n"
)
# The active draw budget a DMA discards: SCU_CheckVDP1HaltKludge() says a DMA is writing VDP1, and
# Update() then throws the positive budget away instead of spending it. Counted only while a draw is
# active (an idle VDP1 has nothing to lose), and as budget, not as elapsed hold time.
VDP1_PERF_DMA_ANCHOR = r'(//puts\("Kludge"\);[ \t]*\n)'
VDP1_PERF_DMA_HOOK = "  if(DrawingActive) SeSsPerfC[SE_PERF_VDP1_DMA_DISCARDED] += (unsigned long long)CycleCounter;   /* > 0 here */\n"
VDP1_PERF_CMD_ANCHOR = r'(memcpy\(CommandData, &VRAM\[CurCommandAddr\], sizeof\(CommandData\)\);[ \t]*\n)'
VDP1_PERF_CMD_HOOK = "   ++SeSsPerfC[SE_PERF_VDP1_COMMANDS];\n"
VDP1_PERF_START_ANCHOR = r'(static void StartDrawing\(void\)\s*\{[ \t]*\n)'
VDP1_PERF_START_HOOK = (
    " ++SeSsPerfC[SE_PERF_VDP1_DRAW_STARTS];\n"
    " if(DrawingActive) ++SeSsPerfC[SE_PERF_VDP1_OVERRUNS];   /* the previous table was not finished */\n"
)
VDP1_PERF_ABORT_ANCHOR = r'(SS_DBGTI\(SS_DBG_WARNING \| SS_DBG_VDP1, "\[VDP1\] Drawing aborted by framebuffer swap\."\);[ \t]*\n)'
VDP1_PERF_ABORT_HOOK = "     ++SeSsPerfC[SE_PERF_VDP1_OVERRUNS];   /* SE_PERF_FBSWAP_ABORT */\n"

# F5 (v27): 68000 idle and executed time.
#
# Idle is counted where the core spends it. M68K::InternalStep() has one place that burns time
# without executing an instruction: after the reset / bus-error / interrupt branches have returned,
# an XPending that is only STOPPED or one of the halt bits falls through to `timestamp += 4`. A
# per-instance accumulator on the core (no Saturn Explorer linkage: the core is shared with other
# systems and the SSF player, and just gains one counter) is bumped by the same 4 there, so total
# time and idle time cannot disagree about a step. The previous scheme sampled "stopped" once before
# a slice of many steps, so a STOP reached or left partway through a slice was charged wholesale to
# one side.
M68K_DIR = os.path.join("src", "hw_cpu", "m68k")
M68K_FILES = ["m68k.h", "m68k.cpp"]
M68K_IDLE_MEMBER_ANCHOR = r'( uint32 XPending;[ \t]*\n)'
M68K_IDLE_MEMBER = (
    " /* Saturn Explorer profiler (F5): cycles InternalStep() spent stopped or halted, running total\n"
    "    (unsigned wrap is fine: callers take differences). Not part of the savestate. */\n"
    " uint32 IdleCycleCount;\n"
)
M68K_IDLE_INIT_ANCHOR = r'( timestamp = 0;[ \t]*\n XPending = 0;[ \t]*\n)'
M68K_IDLE_INIT = " IdleCycleCount = 0;\n"
M68K_IDLE_STEP_ANCHOR = r'(// STOP and ExtHalted fallthrough:[ \t]*\n[ \t]*timestamp \+= 4;[ \t]*\n)'
M68K_IDLE_STEP = "  IdleCycleCount += 4;   /* Saturn Explorer profiler (F5): the same 4 cycles, as idle */\n"

# sound.cpp: SOUND_Update() runs the 68000 in slices between SCSP samples, and the CPU's bus handlers
# abandon an instruction with MDFN_longjmp (four paths: an address error or no-DTACK on a read or a
# write), which lands back at the MDFN_setjmp at the top of SOUND_Update() -- past anything a slice
# did after SoundCPU.Run(). So the baselines (entry timestamp, entry idle count) are taken ONCE,
# before the setjmp, as const locals that nothing writes afterwards (their values therefore
# survive the longjmp), and both deltas are taken once on the normal exit, whatever route got there.
# Executed = total - idle. Not in the SSF player, which has no tap.
SOUND_PERF_FWD = "/* SE_SOUND_PERF_FWD */\n#ifndef MDFN_SSFPLAY_COMPILE\n" + PERF_DECL + "#endif\n"
SOUND_PERF_PRE_ANCHOR = r'(run_until_time \+= \(\(uint64\)\(timestamp - lastts\) \* clock_ratio\);[ \t]*\n lastts = timestamp;[ \t]*\n)'
SOUND_PERF_PRE_HOOK = (
    " const int32 se_perf_ts0 = SoundCPU.timestamp;\n"
    " const uint32 se_perf_idle0 = SoundCPU.IdleCycleCount;\n"
)
SOUND_PERF_POST_ANCHOR = r'(else\s*\{\s*\n\s*while\(next_scsp_time < \(run_until_time >> 32\)\)\s*\n\s*RunSCSP\(\);[ \t]*\n \}[ \t]*\n)'
SOUND_PERF_POST_HOOK = (
    "#ifndef MDFN_SSFPLAY_COMPILE\n"
    " {\n"
    "  const uint32 se_perf_total = (uint32)(SoundCPU.timestamp - se_perf_ts0);\n"
    "  const uint32 se_perf_idle = SoundCPU.IdleCycleCount - se_perf_idle0;\n"
    "  SeSsPerfC[SE_PERF_M68K_TOTAL_CYCLES] += se_perf_total;\n"
    "  SeSsPerfC[SE_PERF_M68K_IDLE_CYCLES] += se_perf_idle;\n"
    "  SeSsPerfC[SE_PERF_M68K_CYCLES] += se_perf_total - se_perf_idle;\n"
    " }\n"
    "#else\n"
    " (void)se_perf_ts0; (void)se_perf_idle0;\n"
    "#endif\n"
)

# ss.cpp: the instruction-cache emulation mode the emulator picked (F8). The bus request counts depend
# on it, so it travels with the sample. SE_PERF_CACHE_ON = instruction fetches go through the emulated
# cache (fills and hits); OFF = they are served from the fast map (the data cache is emulated in
# every mode). The variable is defined in the glue.
SS_CACHE_MODE_ANCHOR = r'(NeedEmuICache = \(cpucache_emumode == CPUCACHE_EMUMODE_FULL\);[ \t]*\n)'
SS_CACHE_MODE_HOOK = " SeSsCacheMode = NeedEmuICache ? SE_PERF_CACHE_ON : SE_PERF_CACHE_OFF;   /* Saturn Explorer profiler (F8) */\n"

# F2 / F9: power edges and resets of the SH-2s, and exception entries. SetActive's two real transitions
# (a redundant call changes nothing and is no edge), the top of Reset on a powered CPU (watchdog or
# SetActive's own), and every exception the core takes that the shadow stack cannot see as an
# instruction. TRAPA is deliberately not hooked: the glue records it as the call it is.
SH2_SETACTIVE_ON_ANCHOR = (r'(void SH7095::SetActive\(bool active\)\s*\{\s*\n if\(active && timestamp == SS_EVENT_DISABLED_TS\)\s*\{\s*\n'
                           r'  timestamp = SH7095_mem_timestamp;[ \t]*\n)')
SH2_SETACTIVE_ON_HOOK = "  if(this == &CPU[1]) SeMednafenSlaveActive(1, (int)timestamp);   /* SE_SLAVE_ON_EDGE */\n"
SH2_SETACTIVE_OFF_ANCHOR = r'(else if\(!active && timestamp != SS_EVENT_DISABLED_TS\)\s*\{[ \t]*\n)'
SH2_SETACTIVE_OFF_HOOK = "  if(this == &CPU[1]) SeMednafenSlaveActive(0, (int)timestamp);   /* SE_SLAVE_OFF_EDGE */\n"
SH2_RESET_ANCHOR = r'(void NO_INLINE SH7095::Reset\(bool power_on_reset, bool from_internal_wdt\)\s*\{\s*\n if\(timestamp == SS_EVENT_DISABLED_TS\)\s*\n  return;[ \t]*\n)'
SH2_RESET_HOOK = " SeMednafenCpuReset((int)(this - CPU), (int)timestamp);   /* SE_CPU_RESET_HOOK */\n"
SH2_EXCEPTIONS = ("INT", "NMI", "CPUADDR", "DMAADDR", "ILLINSTR", "ILLSLOT")


def sh2_exception_edit(kind):
    return (r'(\bException\(EXCEPTION_%s, [^;\n]*;[ \t]*\n)' % kind,
            "\t SeMednafenExceptionEntry((int)(this - CPU));   /* SE_OWN_EXC_%s */\n" % kind,
            "SE_OWN_EXC_%s" % kind)


# File-scope forward declarations (extern "C" is illegal inside a function body, so
# the call sites below are plain calls that resolve to these). Prepended at BOF.
FWD_DECLS = (
    "extern \"C\" void SeMednafenFrameHook(void);\n"
    "extern \"C\" void SeMednafenEndFrameHook(void);\n"
    "extern \"C\" void SeExportNotifyStateLoaded(void);\n"
    # F2 + F6 + F9 lifecycle (se_mednafen_glue.c): slave power edges, CPU resets, exception entries, loads, frame close.
    "extern \"C\" void SeMednafenSlaveActive(int on, int ts);\n"
    "extern \"C\" void SeMednafenCpuReset(int cpu, int ts);\n"
    "extern \"C\" void SeMednafenExceptionEntry(int cpu);\n"
    "extern \"C\" void SeMednafenStateLoaded(int slaveOn);\n"
    "extern \"C\" void SeMednafenFrameClose(int end_ts, int ts0, int ts1, int slaveOn);\n"
    "extern \"C\" int  SeExportGateFrame(void);\n"
    "extern \"C\" int  SeExportGateHalt(void);\n"
    "extern \"C\" void SeExportNotifyStop(int cpu, unsigned int pc);\n"
    "extern \"C\" void SeExportNotifyStep(int cpu, unsigned int pc);\n"
    "extern \"C\" int  SeExportInsnStepBegin(void);\n"
    "extern \"C\" void SeExportNotifyDmaStop(int cpu, unsigned int pc);\n"
    "extern \"C\" int  SeExportInsnStepTick(int cpu, unsigned int pc, int selfBranchTaken);\n"
    "extern \"C\" void SeMednafenTraceHook(int cpu, unsigned int pc);\n"
    "extern \"C\" void SeMednafenHookMode(int continuous);\n"
    "extern \"C\" int  SeMednafenSelfBranchTaken(int cpu, unsigned int pc);\n"
    # debug.inc (patched, see process_debug) runs DBG_CPUHandler for the executing CPU while these say so.
    "extern \"C\" int  SeSsBothCpus;\n"
    "extern \"C\" int  SeSsExecCpu;\n"
    # SCU-DMA watchpoint bridge: scu.inc (included before debug.inc, where DBG lives) calls
    # SeSsDmaWatch — defined at EOF after debug.inc — gated on the SeSsMemWatchArmed flag.
    "extern \"C\" int  SeSsMemWatchArmed;\n"
    "extern \"C\" void SeSsDmaWatch(unsigned int A, unsigned int len, int isWrite);\n"
) + PERF_DECL + PERF_BUS_HELPER

# ---- The slave SH-2 under the debugger. ss/debug.inc's DBG_CPUHandler(which, ...) returns at its first
#      line when `which != DBG.ActiveCPU`, and ActiveCPU is the debugger UI's "current CPU" -- the master
#      unless that UI changed it, which nothing in this integration does. So with a breakpoint,
#      tracepoint or step armed, only the master ever reached our callback. This makes the handler run
#      for whichever CPU is executing while SeSsBothCpus is set (SeSyncCpuHook sets it exactly while our
#      callback is installed): the CPU becomes the "active" one for the duration of the call -- so
#      everything the handler does downstream of that line sees the invariant it was written under,
#      which == ActiveCPU -- and the previous value is restored on every exit path by a scope guard.
#      The guard is inserted at the TOP of the function, ahead of the stock filter, which is left
#      untouched (so --revert only has to remove our block). The helper is a template because the
#      type of DBG.ActiveCPU is the fork's to choose. ----
DEBUG_SCOPE_HELPER = """\
/* Saturn Explorer: make the executing SH-2 the debugger's "active" one for one DBG_CPUHandler call
   (see Integration/Mednafen/apply.py, "The slave SH-2 under the debugger"). */
extern "C" int SeSsBothCpus;
extern "C" int SeSsExecCpu;
/* How deep we are in DBG_CPUHandler, and the hook re-arm that waited for it to return (ss.cpp). The
   handler tests DBG.CPUHook, then runs ForceEventUpdates -- which can reach MidSync, publish a frame
   and apply a breakpoint/tracepoint edit -- and only then calls the hook: an edit that removed the
   last tracepoint there nulled the hook between the test and the call (a crash on the real fork). */
extern "C" int SeSsInHandler;
extern "C" void SeSsHandlerExit(void);
template<typename T> struct SeSsActiveCpuScope {
   T& ref; T saved;
   SeSsActiveCpuScope(T& active, unsigned int which) : ref(active), saved(active) {
      SeSsExecCpu = (int)which;
      if(SeSsBothCpus) ref = (T)which;
      ++SeSsInHandler;
   }
   ~SeSsActiveCpuScope() { ref = saved; if(--SeSsInHandler == 0) SeSsHandlerExit(); }
};"""
DEBUG_SCOPE_USE = (
    "  /* Saturn Explorer: run this handler for the executing SH-2, not only the debugger's active one. */\n"
    "  SeSsActiveCpuScope<decltype(DBG.ActiveCPU)> se_active_cpu(DBG.ActiveCPU, (unsigned int)which);\n"
)
DEBUG_HANDLER_ANCHOR = r'(\bDBG_CPUHandler\s*\([^)]*\)\s*(?:noexcept\s*)?\{[ \t]*\n)'
DEBUG_FILE = "debug.inc"

# Per-frame snapshot call, injected after the frame's cycle count is finalized.
FRAME_HOOK = (
    "   /* Saturn Explorer live tap: publish this frame (lazy-starts the server). */\n"
    "   SeMednafenFrameHook();\n"
)
FRAME_ANCHOR = r'(espec->MasterCycles\s*=\s*[^;]*;\s*\n)'

# End-of-frame call, injected as the last statement of Emulate(). The snapshot hook above sits
# inside MidSync() -- FRAME_ANCHOR's first match is there, part-way through the frame's run loop --
# which is right for the frame's pictures but wrong for a savestate: a state is loaded at the TOP
# of the next frame, so the one to save is the state at the END of this one, after the timestamp
# rebase and the rest of the frame's bookkeeping. Anchored on the signature of the function that
# FOLLOWS Emulate (the closing brace just before it is the insertion point) because Emulate has no
# marker of its own at its end. That ties it to the order of the two functions: if a fork moves
# them, apply.py reports ANCHOR MISS and the build would record pictures but no savestates, so a
# miss must be treated as a failed install, not a warning.
END_FRAME_HOOK = (
    " /* Saturn Explorer profiler: this frame's length. (The slave SH-2's on-time is integrated over its\n"
    "    real power edges and closed by SeMednafenFrameClose before the rebase.) */\n"
    " SeSsPerfC[SE_PERF_FRAME_CYCLES] += end_ts;\n"
    " /* Saturn Explorer live tap: the frame is over; take its savestate here (rewind). */\n"
    " SeMednafenEndFrameHook();\n"
)
END_FRAME_ANCHOR = r'(\n)\}\n\nstatic void OutputMIDI\(uint8 v\)'

# F2 + F6: the frame's emulation is over and its timestamps are NOT yet rebased. Closes the slave's open
# on-interval at end_ts and charges each CPU's final sampled interval, both of which the post-rebase end
# hook above could no longer do exactly. After SMPC_EndFrame (which can still change the slave's power).
FRAME_CLOSE_HOOK = (
    " /* Saturn Explorer profiler: close the frame on the pre-rebase clock (slave on-time, last function interval). */\n"
    " SeMednafenFrameClose(end_ts, (int)CPU[0].timestamp, (int)CPU[1].timestamp, CPU[1].timestamp != SS_EVENT_DISABLED_TS);   /* SE_FRAME_CLOSE_HOOK */\n"
)
FRAME_CLOSE_ANCHOR = r'( SMPC_EndFrame\(espec, end_ts\);[ \t]*\n)'

# A native (emulator-initiated) state load replaces the machine: the profiler's frame identity moves
# to a new timeline. Anchored inside StateAction's `if(load)` block, on the last PostStateLoad. A load
# the server itself ordered reaches this too and is recognized there (SeExportNotifyStateLoaded).
STATE_LOADED_HOOK = (
    "  SeExportNotifyStateLoaded();   /* Saturn Explorer: a new profiler timeline begins */\n"
    # PostStateLoad does not call SetActive: the slave's on/off state is whatever the file held (F2).
    "  SeMednafenStateLoaded(CPU[1].timestamp != SS_EVENT_DISABLED_TS);   /* Saturn Explorer: reseed profiler accounting */\n"
)
STATE_LOADED_ANCHOR = r'(CPU\[1\]\.PostStateLoad\([^;]*\);\s*\n)'

# Optional pause/step gate at the top of Emulate() (opt-in: can disturb audio timing).
GATE_HOOK = (
    "   /* Saturn Explorer pause/step gate (remove if it disturbs audio timing). */\n"
    "   while (!SeExportGateFrame()) { }\n"
)
GATE_ANCHOR = r'(\bEmulate\s*\(\s*EmulateSpecStruct\s*\*\s*\w+\s*\)\s*\{\s*\n)'

# SCU-DMA write watchpoint: let a data watchpoint fire on SCU-DMA writes (VDP1/VDP2 VRAM,
# work RAM...), which bypass the SH-2 instruction pipeline the ss debugger's RW breakpoints
# watch. Injected in scu.inc's DMA_Write, after the transfer unit is written (A + sizeof(T)
# are in scope there). Gated on SeSsMemWatchArmed so a DMA with no watchpoint set is untouched.
SCU_DMA_HOOK = (
    "#ifdef WANT_DEBUGGER\n"
    " /* Saturn Explorer: let SCU-DMA writes trip data watchpoints (see SeSsDmaWatch). */\n"
    " if(SeSsMemWatchArmed) SeSsDmaWatch(A, (unsigned int)sizeof(T), 1);\n"
    "#endif\n"
)
SCU_DMA_ANCHOR = r'(d->CurByteCount -= sizeof\(T\);\s*\n)'

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
    """Append fenced `code` at EOF. The blank line that separates it from the file's own last line
    is INSIDE the fence (the block starts with an empty line), so --revert, which deletes whole
    fences, restores the file byte for byte instead of leaving a stray blank line behind."""
    # A block an older apply.py appended has its separator OUTSIDE the fence; move it inside so this
    # tree reverts exactly too.
    for m in FENCE_RE.finditer(text):
        if key in m.group(0):
            if not m.group(0).startswith(BEGIN + "\n\n") and text[max(0, m.start() - 2):m.start()] == "\n\n":
                text = text[:m.start() - 1] + text[m.start():]
            break
    return upsert(text, "\n" + code, key,
                  lambda t, block: t + ("" if t.endswith("\n") else "\n") + block + "\n")


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
    text, n = apply_anchored(text, END_FRAME_ANCHOR, END_FRAME_HOOK, "SeMednafenEndFrameHook();")
    notes.append(n)
    text, n = apply_anchored(text, FRAME_CLOSE_ANCHOR, FRAME_CLOSE_HOOK, "SE_FRAME_CLOSE_HOOK")
    notes.append(n)
    text, n = apply_anchored(text, STATE_LOADED_ANCHOR, STATE_LOADED_HOOK, "SeExportNotifyStateLoaded();")
    notes.append(n)
    if with_pause:
        text, n = apply_anchored(text, GATE_ANCHOR, GATE_HOOK, "while (!SeExportGateFrame())")
        notes.append(n)
    text, n = apply_append(text, SS_ACCESSORS, "SsDbgWramL")
    notes.append(n)
    text, n = apply_append(text, SAVESTATE_ACCESSORS, "SsDbgSaveState")
    notes.append(n)
    text, n = apply_append(text, EMU_SLOT_ACCESSORS, "SsDbgEmuSlotInfo")
    notes.append(n)
    if do_write and text != original:
        open(path, "w", encoding="utf-8", errors="surrogateescape").write(text)
    return notes


def process_scu(src_dir, do_write):
    """Inject the SCU-DMA write-watchpoint check into scu.inc's DMA_Write. The helper it calls
    (SeSsDmaWatch) and the SeSsMemWatchArmed gate flag live in ss.cpp's appended accessors;
    scu.inc is #included into ss.cpp, so they link in the same TU (forward-declared via
    FWD_DECLS, since scu.inc is included before debug.inc where DBG lives)."""
    path = os.path.join(src_dir, "scu.inc")
    if not os.path.isfile(path):
        return ["scu.inc (DMA watchpoint):", "  MISSING  scu.inc"]
    notes = ["scu.inc (DMA watchpoint):"]
    text = original = open(path, encoding="utf-8", errors="surrogateescape").read()
    text, n = apply_anchored(text, SCU_DMA_ANCHOR, SCU_DMA_HOOK, "SeSsDmaWatch(A,")
    notes.append(n)
    if do_write and text != original:
        open(path, "w", encoding="utf-8", errors="surrogateescape").write(text)
    return notes


def process_anchored(src_dir, fname, title, edits, do_write, prepend=None):
    """Apply a list of (anchor, code, key) insertions to one file, with an optional
    (code, key) block prepended first."""
    path = os.path.join(src_dir, fname)
    notes = [f"{fname} ({title}):"]
    if not os.path.isfile(path):
        return notes + [f"  MISSING  {fname}"]
    text = original = open(path, encoding="utf-8", errors="surrogateescape").read()
    if prepend:
        text, n = apply_prepend(text, prepend[0], prepend[1])
        notes.append(n)
    for anchor, code, key in edits:
        text, n = apply_anchored(text, anchor, code, key)
        notes.append(n)
    if do_write and text != original:
        open(path, "w", encoding="utf-8", errors="surrogateescape").write(text)
    return notes


# Fences an EARLIER apply.py (v26, 4b6cd9e) inserted that this one no longer wants. upsert() finds a
# fence by key and leaves it alone ("ok (already)"), so a tree that was patched once and is patched
# again incrementally (Integration/install.py keeps the checkout) would keep the stale text; for the
# VDP1 one that is a compile error (SE_PERF_VDP1_DRAW_CYCLES no longer exists). Each entry is
# (file, substring that identifies the fence and no fence we still write). Removed before any hook is
# applied. A rename of a key is a retirement of the old key plus a new key.
#   vdp1.cpp  SE_PERF_VDP1_DRAW_CYCLES  index 6, elapsed drawing -- retired in v27, never aliased
#   sound.cpp se_perf_t0                the v26 per-slice 68000 hooks (baseline taken after the setjmp,
#                                       so a longjmp skipped the increments); v27 uses se_perf_ts0
#   ss.cpp    SSH2_ON_CYCLES += end_ts  the all-or-nothing slave on-time in the end-of-frame fence
RETIRED_FENCES = [
    ("vdp1.cpp", "SE_PERF_VDP1_DRAW_CYCLES"),
    ("sound.cpp", "se_perf_t0"),
    # The end-of-frame fence of 3269140 and earlier counted the slave all-or-nothing after the rebase
    # (F2 replaces it with SeMednafenFrameClose before the rebase); same key, different text.
    ("ss.cpp", "SeSsPerfC[SE_PERF_SSH2_ON_CYCLES] += end_ts"),
]


def retire_fences_text(text, key):
    """Remove every fenced block containing `key`. Returns (text, count)."""
    pat = re.compile(re.escape(BEGIN) + r".*?" + re.escape(END) + r"\n?", re.DOTALL)
    n = 0
    def drop(m):
        nonlocal n
        if key in m.group(0):
            n += 1
            return ""
        return m.group(0)
    return pat.sub(drop, text), n


def process_retired(src_dir, do_write):
    notes = []
    for fname, key in RETIRED_FENCES:
        path = os.path.join(src_dir, fname)
        if not os.path.isfile(path):
            continue
        text = open(path, encoding="utf-8", errors="surrogateescape").read()
        new, n = retire_fences_text(text, key)
        if n:
            notes.append(f"  - retired     {fname}: {n} obsolete block(s) containing {key}")
            if do_write:
                open(path, "w", encoding="utf-8", errors="surrogateescape").write(new)
    return ["retired v26 hooks:"] + notes if notes else []


def process_profiler(src_dir, do_write):
    """The profiler's counters (v26 hooks; v27 layout, see SePerfCounters.h): see PERF_DECL. A miss here costs one figure in the Profiler
    panel, which then reads zero -- it is reported like any other miss so it is not shipped unnoticed."""
    notes = process_retired(src_dir, do_write)
    notes += process_anchored(src_dir, "sh7095.inc", "profiler: bus accesses", [
        (SH2_BUSWRITE_ANCHOR, SH2_BUSWRITE_HOOK, "SE_PERF_BUS_WRITE"),
        (SH2_BUSREAD_ANCHOR, SH2_BUSREAD_HOOK, "SE_PERF_BUS_READ"),
    ], do_write)
    notes += process_anchored(src_dir, "sh7095_ops.inc", "profiler: SH-2 idle time", [
        (SH2_SLEEP_ANCHOR, SH2_SLEEP_HOOK, "SE_PERF_SLEEP_HOOK"),
        (SH2_STANDBY_ANCHOR, SH2_STANDBY_HOOK, "SE_PERF_STANDBY_HOOK"),
        (SH2_STALL_ANCHOR, SH2_STALL_HOOK, "SE_PERF_STALL_HOOK"),
    ], do_write)
    notes += process_anchored(src_dir, "sh7095.inc", "profiler: slave power + CPU reset", [
        (SH2_SETACTIVE_ON_ANCHOR, SH2_SETACTIVE_ON_HOOK, "SE_SLAVE_ON_EDGE"),
        (SH2_SETACTIVE_OFF_ANCHOR, SH2_SETACTIVE_OFF_HOOK, "SE_SLAVE_OFF_EDGE"),
        (SH2_RESET_ANCHOR, SH2_RESET_HOOK, "SE_CPU_RESET_HOOK"),
    ], do_write)
    notes += process_anchored(src_dir, "sh7095_ops.inc", "profiler: exception entries (function ownership)",
                              [sh2_exception_edit(k) for k in SH2_EXCEPTIONS], do_write)
    notes += process_anchored(src_dir, "scu.inc", "profiler: DMA + DSP", [
        (SCU_DMA_PERF_ANCHOR, SCU_DMA_PERF_HOOK, "SE_PERF_DMA_ROUTE_BYTES"),
        (SCU_DSP_PERF_ANCHOR, SCU_DSP_PERF_HOOK, "SE_PERF_DSP_CYCLES"),
    ], do_write)
    notes += process_anchored(src_dir, "vdp1.cpp", "profiler: draw time", [
        (VDP1_PERF_CMD_ANCHOR, VDP1_PERF_CMD_HOOK, "SE_PERF_VDP1_COMMANDS"),
        (VDP1_PERF_START_ANCHOR, VDP1_PERF_START_HOOK, "SE_PERF_VDP1_DRAW_STARTS"),
        (VDP1_PERF_ABORT_ANCHOR, VDP1_PERF_ABORT_HOOK, "SE_PERF_FBSWAP_ABORT"),
        (VDP1_EAT_ANCHOR, VDP1_EAT_HOOK, "SeSsVdp1Debit"),
        (VDP1_PERF_DMA_ANCHOR, VDP1_PERF_DMA_HOOK, "SE_PERF_VDP1_DMA_DISCARDED"),
    ], do_write, prepend=(VDP1_PERF_FWD, "SE_VDP1_PERF_FWD"))
    notes += process_anchored(src_dir, "sound.cpp", "profiler: 68000 time", [
        (SOUND_PERF_PRE_ANCHOR, SOUND_PERF_PRE_HOOK, "se_perf_idle0 ="),
        (SOUND_PERF_POST_ANCHOR, SOUND_PERF_POST_HOOK, "SE_PERF_M68K_IDLE_CYCLES"),
    ], do_write, prepend=(SOUND_PERF_FWD, "SE_SOUND_PERF_FWD"))
    m68k_dir = os.path.normpath(os.path.join(src_dir, "..", "hw_cpu", "m68k"))
    notes += process_anchored(m68k_dir, "m68k.h", "profiler: 68000 idle accumulator", [
        (M68K_IDLE_MEMBER_ANCHOR, M68K_IDLE_MEMBER, "IdleCycleCount;"),
    ], do_write)
    notes += process_anchored(m68k_dir, "m68k.cpp", "profiler: 68000 idle accumulator", [
        (M68K_IDLE_INIT_ANCHOR, M68K_IDLE_INIT, "IdleCycleCount = 0;"),
        (M68K_IDLE_STEP_ANCHOR, M68K_IDLE_STEP, "IdleCycleCount += 4;"),
    ], do_write)
    notes += process_anchored(src_dir, "ss.cpp", "profiler: cache mode", [
        (SS_CACHE_MODE_ANCHOR, SS_CACHE_MODE_HOOK, "SeSsCacheMode ="),
    ], do_write)
    return notes


def process_debug(src_dir, do_write):
    """Let the debugger's per-instruction handler run for the slave SH-2 as well as the master (see
    DEBUG_SCOPE_HELPER). Anchored on the DEFINITION of DBG_CPUHandler -- the signature through its opening
    brace -- not on the filter line inside it, whose exact spelling is the fork's. A miss is reported
    loudly: without this patch breakpoints, tracepoints and the shadow call stack silently cover only
    the master."""
    path = os.path.join(src_dir, DEBUG_FILE)
    if not os.path.isfile(path):
        return ["debug.inc (slave SH-2 handler):", "  MISSING  debug.inc"]
    notes = ["debug.inc (slave SH-2 handler):"]
    text = original = open(path, encoding="utf-8", errors="surrogateescape").read()
    text, n = apply_prepend(text, DEBUG_SCOPE_HELPER, "SeSsActiveCpuScope {")
    notes.append(n)
    text, n = apply_anchored(text, DEBUG_HANDLER_ANCHOR, DEBUG_SCOPE_USE, "SeSsActiveCpuScope<decltype")
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
    text, n = apply_anchored(text, VDP1_RESET_ANCHOR, VDP1_RESET_HOOK, "SE_VDP1_LATCH_RESET")
    notes.append(n)
    text, n = apply_anchored(text, VDP1_LOAD_ANCHOR, VDP1_LOAD_HOOK, "SE_VDP1_LATCH_LOAD")
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
              ("vdp1.cpp", "vdp2.cpp", "ss.cpp", "scu.inc", "debug.inc", "sound.cpp", "scsp.h", "cdb.cpp", "smpc.cpp", "smpc.h",
               "sh7095.inc", "sh7095_ops.inc")]
    for f in M68K_FILES:   # the shared 68000 core (F5), outside src/ss
        edited.append(os.path.join(root, M68K_DIR, f))
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


def check_upstream_version(root):
    """Warn when the checkout is not the Mednafen version this patch is recorded against
    (UPSTREAM.json). A warning, not an error: --mednafen-rev exists to try a newer base, and
    the anchors below fail loudly on their own if that base moved them."""
    try:
        with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "UPSTREAM.json"), encoding="utf-8") as f:
            want = json.load(f)["upstream_version"]
        with open(os.path.join(root, "configure.ac"), encoding="utf-8", errors="replace") as f:
            m = re.search(r"^MEDNAFEN_VERSION='([^']+)'", f.read(), re.M)
    except (OSError, ValueError, KeyError):
        return
    have = m.group(1) if m else None
    if have and have != want:
        print(f"warning: checkout is Mednafen {have}, but UPSTREAM.json records {want}. "
              f"Update UPSTREAM.json (and re-verify the hooks) if this is a deliberate bump.\n")


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

    check_upstream_version(root)

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
    notes += process_scu(src_dir, do_write)
    notes += process_debug(src_dir, do_write)
    notes += process_vdp1_drawend(src_dir, do_write)
    notes += process_sound(src_dir, do_write)
    notes += process_cd(src_dir, do_write)
    notes += process_profiler(src_dir, do_write)
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
