/* Saturn Explorer — Savestate / memory-dump driver (Seam A reference driver).
 *
 * The offline reference data source. Two input shapes (per the app's design):
 *   1. A directory of labeled region files.
 *   2. A single linear memory dump, sliced into regions by the documented
 *      Saturn memory map (Docs/Saturn/MemoryLayout.txt).
 *
 * Both populate the same internal region buffers, so the core sees an identical
 * se_data_source either way. Factory functions return 0 on success and fill
 * '*out'; on failure they return non-zero and leave '*out' zeroed.
 */
#ifndef SE_SAVESTATE_DRIVER_H
#define SE_SAVESTATE_DRIVER_H

#include "saturnexplorer/se_data_source.h"

#ifdef __cplusplus
extern "C" {
#endif

/* All three factories return SE_OK and fill '*out' on success, or an se_result
 * error code otherwise (and leave '*out' zeroed). */

/* Region-file directory. Recognized filenames (missing files -> capability off):
 *   vdp1_vram.bin, vdp2_vram.bin, cram.bin,
 *   wram_low.bin, wram_high.bin, vdp1_regs.bin, vdp2_regs.bin
 */
se_result se_savestate_open_region_dir(const char* dir, se_data_source* out);

/* Single linear dump. 'base_address' is the SH-2 bus address of the dump's
 * first byte; any documented region fully contained in the dump is extracted. */
se_result se_savestate_open_full_dump(const char* path, uint32_t base_address,
                                      se_data_source* out);

/* Yabause savestate (.yss). Parses the VDP1 and VDP2 sections for VRAM, CRAM,
 * and — crucially — the VDP2 register file (the write-only state a RAM dump
 * can't provide). Currently targets the Yabause 0.9.15 section/struct layout. */
se_result se_savestate_open_yss(const char* path, se_data_source* out);

#ifdef __cplusplus
}
#endif

#endif /* SE_SAVESTATE_DRIVER_H */
