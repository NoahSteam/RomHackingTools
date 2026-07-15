/* Saturn Explorer — ABI constants, result codes, capability bits.
 *
 * This header defines the version and enum vocabulary shared by both seams:
 *   - Seam A (SeDataSource.h): driver -> core
 *   - Seam B (SeHost.h):        core  -> host
 *
 * Everything here is plain C. No C++ types, STL, or exceptions cross a seam.
 * See ARCHITECTURE.md §3 for the rules.
 */
#ifndef SATURNEXPLORER_SE_ABI_H
#define SATURNEXPLORER_SE_ABI_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bumped on any breaking change to either seam struct. A host/driver compares
 * its compiled-in value against the core's se_abi_version() before use. */
#define SE_ABI_VERSION 2u

/* Result codes. 0 == success; negatives are errors. */
typedef enum se_result {
    SE_OK                 = 0,
    SE_ERR_UNIMPLEMENTED  = -1,   /* function exists but has no logic yet (M1 stubs) */
    SE_ERR_INVALID_ARG    = -2,
    SE_ERR_ABI_MISMATCH   = -3,   /* data source / core ABI versions differ */
    SE_ERR_NO_CAPABILITY  = -4,   /* driver does not advertise the needed capability */
    SE_ERR_OUT_OF_RANGE   = -5,   /* index past the end of a collection */
    SE_ERR_BUFFER_TOO_SMALL = -6, /* caller buffer smaller than *inout size */
    SE_ERR_NO_DATA        = -7,   /* nothing to return (e.g. no frame snapshotted yet) */
    SE_ERR_IO             = -8,   /* underlying read/file error in the driver */
    SE_ERR_UNSUPPORTED    = -9    /* well-formed request the core can't satisfy */
} se_result;

/* Capabilities a driver advertises in se_data_source.capabilities. The core
 * queries these and degrades gracefully when one is absent (e.g. a static dump
 * has no event stream). See ARCHITECTURE.md §4. */
enum {
    SE_CAP_VDP1_VRAM   = 1u << 0,
    SE_CAP_VDP2_VRAM   = 1u << 1,
    SE_CAP_CRAM        = 1u << 2,   /* VDP2 color RAM / palette */
    SE_CAP_MAIN_RAM    = 1u << 3,   /* Work RAM Low + High */
    SE_CAP_VDP1_REGS   = 1u << 4,
    SE_CAP_VDP2_REGS   = 1u << 5,
    SE_CAP_FRAMEBUFFER = 1u << 6,   /* optional reference frame, for diffing vs core render */
    SE_CAP_EVENT_STREAM= 1u << 7,   /* memory-write / DMA events (Memory History) */
    SE_CAP_DISC        = 1u << 8,   /* raw ISO / file access (ROM Search) */
    SE_CAP_FRAME_STEP  = 1u << 9,   /* pause / step / advance (Frame Timeline) */
    SE_CAP_SYSTEM_STATUS = 1u << 10 /* SH-2 PC, scanline, blank flags (status bar) */
    /* Reserved for a future live driver: SE_CAP_BREAKPOINTS ("Break on Read"). */
};

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SATURNEXPLORER_SE_ABI_H */
