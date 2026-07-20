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
void SeExportSnapshot(const void* vdp1_vram_512k, const void* vdp2_vram_512k,
                      const void* cram_4k, const void* vdp2_regs_struct_288,
                      const void* vdp1_regs_struct, const void* wram_low_1m,
                      const void* wram_high_1m, const void* vdp1_fb_256k,
                      const void* msh2_regs, const void* ssh2_regs);

/* Wire the module's breakpoint installers to Yabause's SH2 breakpoint API (v5+).
 * 'add' installs one execution breakpoint: add(cpu, address) with cpu 0 = master,
 * 1 = slave. 'clear' removes all breakpoints. Both may be NULL (breakpoints then
 * round-trip over the protocol but don't install). Call once after SeExportInit,
 * e.g. SeExportSetBreakpointHooks(SeYabauseAddExecBp, SeYabauseClearBps). */
void SeExportSetBreakpointHooks(void (*add)(int cpu, unsigned int address),
                                void (*clear)(void));

/* Call from Yabause's SH2 breakpoint callback when the master/slave core hits an
 * execution breakpoint (cpu 0 = master, 1 = slave; pc = the halted PC). Latches a
 * stop event and holds the emulator paused; the next snapshot reports it so the
 * debugger can jump to the PC. A resume / single-step from the debugger clears it. */
void SeExportNotifyStop(int cpu, unsigned int pc);

/* Wire the module's work-RAM poke to the emulator's byte writer (v6+), so the Hex
 * Editor can edit a running game: write(address, value) writes one byte. On this
 * Yabause that's MappedMemoryWriteByteNocache(MSH2, addr, val) — MappedMemoryWriteByte
 * is only a function-pointer field on SH2_struct, not a callable function. May be
 * NULL (writes are then dropped). Writing byte-by-byte at Saturn addresses preserves
 * big-endian order without a manual swap. Call once after SeExportInit, e.g.
 * SeExportSetMemWriteHook(SeYabauseWriteByte). */
void SeExportSetMemWriteHook(void (*write)(unsigned int address, unsigned char value));

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
