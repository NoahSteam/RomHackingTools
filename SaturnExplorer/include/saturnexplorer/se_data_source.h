/* Saturn Explorer — Seam A: the Data Source interface (driver -> core).
 *
 * A driver fills in this struct and hands it to se_create(). The core calls
 * these callbacks to read live Saturn state and the game disc. Not every driver
 * can provide everything; it advertises what it supports in 'capabilities'
 * (SE_CAP_* in se_abi.h), and the core adapts. See ARCHITECTURE.md §4.
 *
 * Rules: only C types cross this boundary; 'user' is the driver's own context,
 * passed back to every call. Bulk readers return the number of bytes actually
 * copied. Optional callbacks (guarded by a capability bit) may be NULL when the
 * bit is clear.
 */
#ifndef SATURNEXPLORER_SE_DATA_SOURCE_H
#define SATURNEXPLORER_SE_DATA_SOURCE_H

#include "se_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct se_data_source {
    uint32_t abi_version;   /* set to SE_ABI_VERSION */
    uint32_t capabilities;  /* bitmask of SE_CAP_* */
    void*    user;          /* driver context, passed back to every callback */

    /* --- Bulk memory reads. Return bytes copied (may be < size on error). --- */
    size_t (*read_vdp1_vram)(void* user, uint32_t offset, void* dst, size_t size);
    size_t (*read_vdp2_vram)(void* user, uint32_t offset, void* dst, size_t size);
    size_t (*read_cram)     (void* user, uint32_t offset, void* dst, size_t size);
    size_t (*read_main_ram) (void* user, uint32_t address, void* dst, size_t size);

    /* --- Registers. --- */
    uint16_t (*read_vdp1_reg)(void* user, uint32_t reg);
    uint16_t (*read_vdp2_reg)(void* user, uint32_t reg);

    /* --- Optional: reference framebuffer (SE_CAP_FRAMEBUFFER) for diffing vs
           the core's own software render. NOT the primary display path — the
           core composites the frame itself from VRAM (see ARCHITECTURE.md §7). */
    int (*get_framebuffer)(void* user, se_framebuffer* out);

    /* --- Optional: memory-access event pump (SE_CAP_EVENT_STREAM). Fills up to
           'max' events, returns the count. Powers Memory History / Asset Trace. */
    size_t (*poll_events)(void* user, se_mem_event* out, size_t max);

    /* --- Optional: disc / archive access (SE_CAP_DISC). --- */
    int    (*disc_stat)(void* user, se_disc_info* out);
    size_t (*disc_read)(void* user, uint64_t byte_offset, void* dst, size_t size);

    /* --- Optional: frame control (SE_CAP_FRAME_STEP). --- */
    int      (*frame_pause) (void* user);
    int      (*frame_step)  (void* user, int32_t frames);
    uint64_t (*frame_number)(void* user);

    /* --- Optional: system status (SE_CAP_SYSTEM_STATUS) for the status bar. --- */
    int (*get_system_status)(void* user, se_system_status* out);

    /* --- Optional lifecycle: the core calls this on se_destroy so the driver
           can release its resources. May be NULL. --- */
    void (*close)(void* user);
} se_data_source;

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SATURNEXPLORER_SE_DATA_SOURCE_H */
