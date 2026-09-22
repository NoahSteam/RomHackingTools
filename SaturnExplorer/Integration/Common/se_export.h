/* Saturn Explorer — portable live-tap memory-export server (drop-in module).
 *
 * Emulator-agnostic: SeExportSnapshot takes raw pointers, so this module needs none
 * of any emulator's headers or types. Each emulator's patcher (Integration/<emu>/)
 * copies this file + se_export.c + SeLiveProtocol.h into that emulator's source and
 * wires four calls into its frame loop:
 *   - SeExportInit()       at emulator init
 *   - SeExportSnapshot(...) once per frame, after the frame is drawn
 *   - SeExportGateFrame()  in the run loop, to honor pause / single-step
 *   - SeExportDeinit()     at emulator shutdown
 * (For Yabause those hook sites are YabauseInit / Vdp2VBlankOUT / the run loop /
 * YabauseDeInit — see Integration/Yabause/README.md.)
 *
 * It serves the current VDP1/VDP2 VRAM, CRAM, and VDP2 register struct to Saturn
 * Explorer's LiveDriver over a local socket (Unix domain socket / named pipe).
 */
#ifndef SE_EXPORT_H
#define SE_EXPORT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* SaturnExplorer tap version — the "SE version" shown in the patched emulator's
 * window title. Bump when the tap's behaviour changes. */
#define SE_EXPORT_VERSION "1.0"

/* Returns a static, reusable string to append to the emulator's window title,
 * marking the build as SaturnExplorer-tapped, e.g.
 *   "(SaturnExplorer Enabled. 1.0 / Mednafen 1.32.1)"
 * emu_name / emu_rev are the emulator's own name + version it was built against
 * (pass its version macro, e.g. MEDNAFEN_VERSION or VERSION); either may be NULL.
 * The returned pointer is to a shared static buffer — copy it if you need to keep it. */
const char* SeExportTitleSuffix(const char* emu_name, const char* emu_rev);

/* Start the export server (background thread + local socket). Returns 0 on
 * success, non-zero on failure (Yabause keeps running either way). */
int SeExportInit(void);

/* Copy the current Saturn memory into the export double-buffer. Call once per
 * frame, e.g. at the end of Vdp2VBlankOUT(), passing Yabause's globals:
 *   sh2regs_struct m, s;
 *   SH2GetRegisters(MSH2, &m); SH2GetRegisters(SSH2, &s);
 *   SeExportSnapshot(Vdp1Ram, Vdp2Ram, Vdp2ColorRam, Vdp2Regs,
 *                    Vdp1Regs, LowWram, HighWram, VIDSoftGetVdp1FrameBuffer(),
 *                    &m, &s);
 * Sizes are fixed by the hardware. 'vdp1_regs_struct' is Yabause's `Vdp1` struct
 * (its first 11 u16 fields TVMR..MODR); this module builds the hardware-offset
 * register image from it. 'vdp1_fb_256k' is the VDP1 frame buffer (drawn output,
 * 256 KiB RGB555). Note: the *global* `Vdp1FrameBuffer` is only a fallback in
 * Yabause — real pixels live in the active video core, so apply.py adds a tiny
 * VIDSoftGetVdp1FrameBuffer() accessor returning VIDSoft's displayed front bank.
 * (VIDOGL keeps pixels on the GPU and would need a read-back instead.) 'msh2_regs'
 * and 'ssh2_regs' are Yabause `sh2regs_struct` values (R[16], SR, GBR, VBR, MACH,
 * MACL, PR, PC — 23 u32) for the master and slave SH-2, feeding the disassembler /
 * Assembly panel. Any argument may be NULL (that section is zeros). */
/* 'sound_ram_512k' (v13+) is the emulator's 512 KiB SCSP sound RAM (the 68000 sound
 * program + PCM tone bank + sequences); NULL zero-fills the sound-RAM block. It is the
 * last argument so older call sites that don't pass it still compile if recompiled — but
 * the wire block is only served when the argument is non-NULL. */
/* 'scsp_slots_block' (v14+) is SE_LIVE_SCSP_BLOCK_LEN bytes: 32 decoded SCSP voice records
 * in the fixed little-endian layout documented in SeLiveProtocol.h. The emulator glue builds
 * it from its own decoded slot struct. NULL omits the slot block (the Sound panel then shows
 * no voices). Last arg for the same forward-compat reason as sound_ram_512k. */
/* 'cd_status_block' (v15+) is SE_LIVE_CD_BLOCK_LEN bytes: the CD-block status record in the
 * fixed little-endian layout documented in SeLiveProtocol.h (current FAD + play range +
 * state). NULL omits it (Disc Explorer shows no live read position). Last arg, same forward-
 * compat reason. */
void SeExportSnapshot(const void* vdp1_vram_512k, const void* vdp2_vram_512k,
                      const void* cram_4k, const void* vdp2_regs_struct_288,
                      const void* vdp1_regs_struct, const void* wram_low_1m,
                      const void* wram_high_1m, const void* vdp1_fb_256k,
                      const void* msh2_regs, const void* ssh2_regs,
                      const void* sound_ram_512k, const void* scsp_slots_block,
                      const void* cd_status_block);

/* Wire the module's breakpoint installers to Yabause's SH2 breakpoint API (v5+).
 * 'add' installs one execution breakpoint: add(cpu, address) with cpu 0 = master,
 * 1 = slave. 'clear' removes all breakpoints. Both may be NULL (breakpoints then
 * round-trip over the protocol but don't install). Call once after SeExportInit,
 * e.g. SeExportSetBreakpointHooks(SeYabauseAddExecBp, SeYabauseClearBps). */
void SeExportSetBreakpointHooks(void (*add)(int cpu, unsigned int address),
                                void (*clear)(void));

/* Wire the module's memory (data) breakpoint installer (v5+). add(cpu, address, size,
 * kind) installs one watchpoint over [address, address+size) with kind 1 = read,
 * 2 = write, 3 = read/write; size is 1/2/4 bytes (the Saturn's 8/16/32-bit accesses).
 * SeExportSetBreakpointHooks' 'clear' also clears these. May be NULL (memory
 * breakpoints then round-trip over the protocol but don't install). */
void SeExportSetMemBreakpointHook(void (*add)(int cpu, unsigned int address,
                                              unsigned int size, unsigned int kind));

/* Call from Yabause's SH2 breakpoint callback when the master/slave core hits an
 * execution breakpoint (cpu 0 = master, 1 = slave; pc = the halted PC). Latches a
 * stop event and holds the emulator paused; the next snapshot reports it so the
 * debugger can jump to the PC. A resume / single-step from the debugger clears it. */
void SeExportNotifyStop(int cpu, unsigned int pc);

/* Instruction stepping (v12+). The emulator's per-instruction debug hook drives these:
 *  - SeExportNotifyStep(cpu, pc): latch a stop as SE_LIVE_STOP_STEP (a completed step),
 *    the step analog of SeExportNotifyStop.
 *  - SeExportInsnStepBegin(): call right after the halt gate releases; returns 1 if an
 *    instruction step (IST verb) was requested, activating its budget — the caller then
 *    arms continuous per-instruction hooking.
 *  - SeExportInsnStepTick(cpu, pc): call from the per-instruction hook with the CPU's current
 *    PC; returns 1 when the step budget is spent (halt here). Only the stepped CPU is counted,
 *    and only retired instructions count (a repeated PC — the SH-2 bus-stalled behind a DMA —
 *    does not spend budget), so a step from a DMA-watchpoint halt advances one real instruction
 *    instead of pinning the PC. */
void SeExportNotifyStep(int cpu, unsigned int pc);
int  SeExportInsnStepBegin(void);
int  SeExportInsnStepTick(int cpu, unsigned int pc);

/* Wire the module's work-RAM poke to the emulator's byte writer (v6+), so the Hex
 * Editor can edit a running game: write(address, value) writes one byte. On this
 * Yabause that's MappedMemoryWriteByteNocache(MSH2, addr, val) — MappedMemoryWriteByte
 * is only a function-pointer field on SH2_struct, not a callable function. May be
 * NULL (writes are then dropped). Writing byte-by-byte at Saturn addresses preserves
 * big-endian order without a manual swap. Call once after SeExportInit, e.g.
 * SeExportSetMemWriteHook(SeYabauseWriteByte). */
void SeExportSetMemWriteHook(void (*write)(unsigned int address, unsigned char value));

/* Wire a sound-RAM byte poke (v13+), so the Hex Editor's Sound RAM tab / the music-swap
 * prototype can write the running game's SCSP RAM. write(offset, value) writes one byte at
 * a 0-based offset within the 512 KiB sound RAM. May be NULL (sound-RAM writes are then
 * dropped). Call once after SeExportInit, e.g. SeExportSetSoundWriteHook(SeMednafenWriteSoundByte). */
void SeExportSetSoundWriteHook(void (*write)(unsigned int offset, unsigned char value));

/* Wire full-savestate save/load (v16+), enabling the rewind timeline's "Play from here":
 * a per-frame savestate ring (delta-compressed on a worker thread) that the client can ask
 * the emulator to restore. Setting the SAVE hook is what turns the feature on.
 *   save(buf, cap): write the current full emulator savestate into buf (up to cap bytes) and
 *     return the number of bytes written; when buf is NULL, return the required size (probe).
 *     Runs on the emulate thread at a frame boundary (called from SeExportSnapshot).
 *   load(buf, len): restore the emulator from a full savestate image; return 0 on success.
 *     Runs on the emulate thread at the frame gate (via the LST verb). May be NULL.
 * Both are byte-opaque to se_export; the glue maps them to e.g. MDFNSS_SaveSM/MDFNSS_LoadSM.
 * Requires the pause gate (SeExportGateFrame) to be installed. Call once after SeExportInit. */
void SeExportSetSaveStateHook(size_t (*save)(unsigned char* buf, size_t cap));
void SeExportSetLoadStateHook(int (*load)(const unsigned char* buf, size_t len));

/* Wire the emulator's OWN numbered save slots (v17+) -- the ones its save-state hotkeys
 * use -- so the client can list and load them without knowing where they live on disk.
 *   info(slot, mtime): 1 if that slot holds a state, else 0; *mtime gets its modification
 *     time in unix seconds (0 if unknown). Called once per slot while building a reply.
 *   load(slot): have the emulator load its own slot; return 0 on success. Runs on the
 *     emulate thread at the frame gate, like the LST restore.
 * Either may be NULL, in which case the inventory reports no slots and the ELS verb is a
 * no-op. Call once after SeExportInit. */
void SeExportSetEmuSlotHooks(int (*info)(unsigned int slot, unsigned long long* mtime),
                             int (*load)(unsigned int slot));

/* Wire controller input injection (v7+), so the Saturn Explorer controller panel can
 * drive the running game directly. set(port, buttons) receives the emulator-agnostic
 * SE_PAD_* bitmask; the glue latches it and feeds the emulated pad for `port`,
 * bypassing the emulator's own host-input mapping. May be NULL (input is dropped).
 * Call once after SeExportInit, e.g. SeExportSetInputHook(SeMednafenSetPad). */
void SeExportSetInputHook(void (*set)(unsigned int port, unsigned int buttons));

/* Wire the emulator's live host keyboard bindings (v10+), so the Saturn Explorer
 * controller panel can mirror the user's keys (e.g. WASD) automatically — no config-file
 * upload. get(port, out[13]) fills out with the USB-HID scancode bound to each Saturn pad
 * button (ascending SE_PAD_* order: UP,DOWN,LEFT,RIGHT,A,B,C,X,Y,Z,L,R,START), -1 where no
 * keyboard key is bound; returns the count matched. May be NULL (the keymap block is then
 * all -1 and the client keeps its own defaults). Call once after SeExportInit. */
void SeExportSetKeyMapHook(int (*get)(unsigned int port, int out[13]));

/* Wire a port device-type query (v12+), so Saturn Explorer can report the emulator's
 * controller configuration for ports 1 & 2 whenever a client connects. get(port) returns
 * a short human-readable device name ("Digital Control Pad", "3D Control Pad", "Mouse",
 * ...) for port 0/1. May be NULL (nothing is logged). Call once after SeExportInit. */
void SeExportSetPortInfoHook(const char* (*get)(unsigned int port));

/* Wire tracepoint installation (v8+). set(count, descs) receives 'count'
 * SE_LIVE_TRACE_DESC_LEN descriptors {id,cpu,address,flags} (u32 LE) whenever the
 * client's tracepoint set changes; the glue arms a PC trap at each enabled address.
 * May be NULL (tracepoints then never fire). */
void SeExportSetTracepointHook(void (*set)(unsigned int count, const unsigned char* descs));

/* Queue a fired tracepoint event (v8+). The glue calls this from the CPU thread when a
 * tracepoint PC is hit, passing the tracepoint id, cpu (0/1), and the captured SH-2
 * registers (23 u32 in se_sh2_regs order: r[0..15],pc,pr,sr,gbr,vbr,mach,macl; NULL
 * queues zeros). The frame is stamped internally. The server drains these into the
 * reply; the client formats the message from the captured registers. */
void SeExportQueueTraceEvent(unsigned int id, unsigned int cpu, const unsigned int* regs);

/* Queue a diagnostic log line (v11+). Any-thread safe; 'msg' is copied and truncated to
 * SE_LIVE_LOG_LINE_LEN-1 characters (NULL ignored). The server drains these into each
 * reply's log block and the client shows them in its Log window — a console-free way for
 * the emulator glue to surface diagnostics (e.g. the controller input it receives). */
void SeExportLog(const char* msg);

/* Shadow call stack (v9+). The glue records control flow as it executes so the client
 * gets a dependable (● Confirmed) call stack instead of reconstructing one heuristically.
 * Call from the CPU thread. 'callSite' is the calling/trapping instruction, 'func' the
 * call target (the frame's function entry), 'ret' the return address, 'sp' R15 for the
 * frame, 'cycle' a monotonic cycle stamp (0 if unavailable). cpu is 0 master / 1 slave.
 * The frame number is stamped internally.
 *
 * A frame is tagged with what created it, and only the matching return may remove it:
 *
 *   bsr/bsrf/jsr    -> SeExportPushFrame           rts -> SeExportPopFrame
 *   exception entry -> SeExportPushExceptionFrame  rte -> SeExportPopExceptionFrame
 *
 * That pairing is not bookkeeping pedantry, it is SH-2 semantics: `rts` returns to PR,
 * which only a call wrote, while `rte` returns to the PC the *hardware* pushed onto R15
 * when it took the exception. An interrupt executes no call instruction, so nothing is
 * ever pushed for it; letting its `rte` pop deleted a frame belonging to the interrupted
 * program, and a Saturn takes many interrupts per emulated frame (VBlank in/out, HBlank,
 * SCU timers, SCSP), so the stack drained to empty within a frame or two. A handler's own
 * calls still push and pop normally, so the depth is back to the interrupted program's by
 * the time the handler returns.
 *
 * SeExportPopExceptionFrame takes R15 at the `rte`. SH-2 `rte` pops PC and SR from R15,
 * so that value equals the 'sp' recorded at the entry it returns from; requiring the
 * match keeps an exception we never observed (an interrupt taken inside a handler we did
 * record) from unwinding a frame that is not its own. An `rte` matching nothing therefore
 * leaves the stack exactly as it is — which is the normal case for a glue that can only
 * see exception entries that are instructions (`trapa`, whose return address is PC+2 and
 * whose entry leaves the handler running at R15 - 8) and not asynchronous interrupts.
 *
 * Depth saturates at SE_CALLSTACK_CAP. Frames past it are counted rather than stored, so
 * their returns unwind the counter instead of deleting a stored frame that is still live.
 * Only `rts` consumes that counter: an overflowed frame kept no kind and no sp, so `rte`
 * cannot tell whether one is its own and declines to guess.
 *
 * SeExportResetCallStack drops a CPU's stack at a discontinuity — anything that replaces
 * the emulated machine's own stack wholesale, e.g. a savestate restore or a rewind.
 * Recorded frames describe a timeline that no longer exists after one. */
#define SE_CALLSTACK_CAP 256u
void SeExportPushFrame(int cpu, unsigned int callSite, unsigned int func,
                       unsigned int ret, unsigned int sp, unsigned long long cycle);
void SeExportPushExceptionFrame(int cpu, unsigned int site, unsigned int handler,
                                unsigned int ret, unsigned int sp, unsigned long long cycle);
void SeExportPopFrame(int cpu);
void SeExportPopExceptionFrame(int cpu, unsigned int sp);
void SeExportResetCallStack(int cpu);

/* Serialize one CPU's shadow stack into the v9 wire block: u32 frameCount (capped at
 * SE_LIVE_CALLSTACK_MAX) then that many SE_LIVE_CALLFRAME_LEN frames, innermost first.
 * 'out' must hold SE_LIVE_CALLSTACK_BLOCK_MAX bytes; returns the bytes written. The
 * whole read happens under one lock hold — the CPU thread pushes and pops between
 * instructions, so a depth sampled and then released would index slots above the live
 * top and ship frames that had already been popped. Exposed (rather than inlined into
 * the server loop) so the recording model is testable without an emulator. */
unsigned int SeExportSerializeCallStack(int cpu, unsigned char* out);

/* Frame gate for pause / single-step. Call once at the top of each emulated
 * frame in Yabause's run loop; returns 1 if the frame should run, 0 if the
 * debugger is holding it paused. When it returns 0 it has already slept ~2 ms
 * internally, so just spin on it — no host sleep needed:
 *   while (!SeExportGateFrame()) { }
 * When resumed or single-stepped from Saturn Explorer, it releases frames again.
 * Returns 1 when the server isn't running, so an un-paused build is unaffected. */
int SeExportGateFrame(void);

/* Stop the server thread and free resources. */
void SeExportDeinit(void);

#ifdef __cplusplus
}
#endif

#endif /* SE_EXPORT_H */
