/* Saturn Explorer — Yabause memory-export server (drop-in module).
 *
 * Add this file + se_export.c + SeLiveProtocol.h to yabause/src, then wire four
 * calls into Yabause (see README.md):
 *   - SeExportInit()      at the end of YabauseInit()
 *   - SeExportSnapshot(...) once per frame, from Vdp2VBlankOUT()
 *   - SeExportGateFrame() in the run loop, to honor pause / single-step
 *   - SeExportDeinit()    in YabauseDeInit()
 *
 * It serves the current VDP1/VDP2 VRAM, CRAM, and VDP2 register struct to Saturn
 * Explorer's LiveDriver over a local socket (Unix domain socket / named pipe).
 * It is self-contained: SeExportSnapshot takes raw pointers, so this module needs
 * none of Yabause's headers or types.
 */
#ifndef SE_EXPORT_H
#define SE_EXPORT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Start the export server (background thread + local socket). Returns 0 on
 * success, non-zero on failure (Yabause keeps running either way). */
int SeExportInit(void);

/* Copy the current Saturn memory into the export double-buffer. Call once per
 * frame, e.g. at the end of Vdp2VBlankOUT(), passing Yabause's globals:
 *   SeExportSnapshot(Vdp1Ram, Vdp2Ram, Vdp2ColorRam, Vdp2Regs,
 *                    Vdp1Regs, LowWram, HighWram, Vdp1FrameBuffer[0]);
 * Sizes are fixed by the hardware. 'vdp1_regs_struct' is Yabause's `Vdp1` struct
 * (its first 11 u16 fields TVMR..MODR); this module builds the hardware-offset
 * register image from it. 'vdp1_fb_256k' is the VDP1 frame buffer (drawn output;
 * Yabause exposes it as Vdp1FrameBuffer[0] — adjust for your fork if it differs).
 * Any argument may be NULL (that section is zeros). */
void SeExportSnapshot(const void* vdp1_vram_512k, const void* vdp2_vram_512k,
                      const void* cram_4k, const void* vdp2_regs_struct_288,
                      const void* vdp1_regs_struct, const void* wram_low_1m,
                      const void* wram_high_1m, const void* vdp1_fb_256k);

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
