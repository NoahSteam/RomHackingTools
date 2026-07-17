/* Saturn Explorer — Yabause memory-export server (drop-in module).
 *
 * Add this file + se_export.c + SeLiveProtocol.h to yabause/src, then wire three
 * calls into Yabause (see README.md):
 *   - SeExportInit()      at the end of YabauseInit()
 *   - SeExportSnapshot(...) once per frame, from Vdp2VBlankOUT()
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

/* Copy the current VDP memory into the export double-buffer. Call once per frame,
 * e.g. at the end of Vdp2VBlankOUT(), passing Yabause's globals:
 *   SeExportSnapshot(Vdp1Ram, Vdp2Ram, Vdp2ColorRam, Vdp2Regs);
 * Sizes are fixed by the hardware (512 KiB / 512 KiB / 4 KiB / 288 bytes). Any
 * argument may be NULL (that section is sent as zeros). */
void SeExportSnapshot(const void* vdp1_vram_512k, const void* vdp2_vram_512k,
                      const void* cram_4k, const void* vdp2_regs_struct_288);

/* Stop the server thread and free resources. */
void SeExportDeinit(void);

#ifdef __cplusplus
}
#endif

#endif /* SE_EXPORT_H */
