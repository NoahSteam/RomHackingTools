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

#include "saturnexplorer/SeDataSource.h"

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

/* Yabause-family savestate (.yss). Parses the VDP1 and VDP2 sections for VRAM,
 * CRAM, and — crucially — the VDP2 register file (the write-only state a RAM dump
 * can't provide). The classic Vdp2 register struct is a fixed hardware mirror
 * shared byte-for-byte across the lineage (verified identical in Yabause 0.9.x,
 * Yaba Sanshiro, and Kronos), so the VDP2 section is recognized structurally
 * rather than by version number — covering those emulators and any fork that kept
 * the layout. A fork that changed the struct/VRAM size is skipped (VDP1-only)
 * rather than misdecoded. */
se_result se_savestate_open_yss(const char* path, se_data_source* out);

/* Mednafen savestate (MDFNSVST container, Saturn 'ss' module). Parses the VDP1
 * and VDP2 sections for VRAM, CRAM, and the raw VDP2 register array (RawRegs).
 * Mednafen stores these as host-endian uint16 arrays; the parser byte-swaps them
 * to the core's Saturn-native big-endian. Also covers RetroArch's Beetle Saturn
 * core, which shares the format. */
se_result se_savestate_open_mednafen(const char* path, se_data_source* out);

/* Generic savestate entry: sniffs the file's magic and dispatches to the parser
 * for that emulator's format, so callers need not know which emulator produced
 * the file. Recognizes the Yabause family (.yss — Yabause 0.9.x, Yaba Sanshiro,
 * and Kronos, whose VDP2 struct is verified identical) and Mednafen/Beetle Saturn
 * (MDFNSVST). SSF is closed-source (no known layout); other formats return
 * SE_ERR_UNSUPPORTED until added. Each parser yields the same se_data_source, so
 * the core is format-agnostic. */
se_result se_savestate_open(const char* path, se_data_source* out);

/* Buffer-based variants for hosts without filesystem access (e.g. a browser/WASM
 * build reading a File into memory). Identical to the path-based functions above
 * but take the savestate/dump bytes directly. The driver copies what it needs, so
 * the caller may free 'data' as soon as the call returns.
 *
 *   se_savestate_open_buffer            -> like se_savestate_open (magic dispatch)
 *   se_savestate_open_full_dump_buffer  -> like se_savestate_open_full_dump
 */
se_result se_savestate_open_buffer(const uint8_t* data, size_t size, se_data_source* out);
se_result se_savestate_open_full_dump_buffer(const uint8_t* data, size_t size,
                                             uint32_t base_address, se_data_source* out);

#ifdef __cplusplus
}
#endif

#endif /* SE_SAVESTATE_DRIVER_H */
