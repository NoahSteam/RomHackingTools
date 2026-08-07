/* Saturn Explorer — Seam B: the Host / Embed API (core -> host).
 *
 * The host (our reference frontend, or a third-party app such as an emulator
 * embedding the core) creates a context around a driver, snapshots a frame,
 * then issues read-only queries. This is the surface the frontend's panels are
 * built on. See ARCHITECTURE.md §5.
 *
 * The core does software rasterization and returns data + decoded images
 * (se_image RGBA). It never makes a graphics API call; the host does all GPU
 * work. Collection getters follow a count() + get(index) pattern; bulk fillers
 * take a caller buffer + max and return how many were written.
 */
#ifndef SATURNEXPLORER_SE_HOST_H
#define SATURNEXPLORER_SE_HOST_H

#include "SeTypes.h"
#include "SeDataSource.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The core's ABI version, for the host to check before anything else. */
uint32_t se_abi_version(void);

typedef struct se_context se_context;   /* opaque */

/* Lifecycle. se_create copies what it needs from 'ds'; the driver must outlive
 * the context (the core calls ds->close on destroy). Returns NULL on failure. */
se_context* se_create (const se_data_source* ds, const se_config* cfg);
void        se_destroy(se_context* ctx);

/* Snapshot current Saturn state; all queries below read this immutable snapshot
 * until the next se_begin_frame. */
se_result   se_begin_frame(se_context* ctx);

/* --- Command Table Explorer / Interactive Sprite Inspection --- */
size_t      se_command_count(se_context* ctx);
se_result   se_get_command (se_context* ctx, size_t index, se_command* out);
se_result   se_hit_test    (se_context* ctx, int x, int y, size_t* out_index);
/* Pick the topmost 3D sprite under (x,y) for 'camera' (the 3D View's camera). */
se_result   se_hit_test_3d (se_context* ctx, const se_camera3d* camera,
                            int x, int y, size_t* out_index);

/* --- VDP1 geometry: every sprite emitted in TWO coordinate spaces (§7). --- */
size_t      se_sprite_count  (se_context* ctx);
se_result   se_get_sprite_2d (se_context* ctx, size_t index, se_sprite_2d* out);
se_result   se_get_sprite_3d (se_context* ctx, size_t index, se_sprite_3d* out);

/* --- Software VDP composite: the finished frame, rasterized by the core from
       the sprite quads + VDP2 layers. 'opts' carries every layer toggle and
       overlay. 'out' is a caller-allocated se_image; pass out->pixels == NULL
       to learn the required size (returned via *needed). --- */
se_result   se_render_frame(se_context* ctx, const se_render_opts* opts,
                            se_image* out, size_t* needed);

/* --- 3D world view: the same sprites, exploded along Z, software-rendered
       from the host-supplied camera into an image of the camera's viewport
       size. Same two-call size convention as se_render_frame. --- */
se_result   se_render_3d(se_context* ctx, const se_camera3d* camera,
                         const se_render_opts* opts, se_image* out, size_t* needed);

/* --- Texture & Palette Viewer --- */
se_result   se_decode_texture(se_context* ctx, const se_texture_ref* ref,
                              se_image* out, size_t* needed);
se_result   se_decode_palette(se_context* ctx, uint32_t clut_address, se_palette* out);

/* Decode the CRAM sub-palette a color-bank sprite indexes into. 'color_bank' is
   the sprite's CMDCOLR; 'color_mode' fixes how many CRAM entries the bank spans
   (16/64/128/256). Returns SE_ERR_UNSUPPORTED for LUT / RGB555 modes, which have
   no CRAM bank palette. */
se_result   se_decode_bank_palette(se_context* ctx, uint16_t color_bank,
                                   se_color_mode color_mode, se_palette* out);

/* --- VRAM Visualization --- */
size_t      se_vram_region_count(se_context* ctx);
se_result   se_get_vram_region  (se_context* ctx, size_t index, se_vram_region* out);

/* --- Raw inspection (register / memory / palette-RAM viewers) --- */
/* Whether the loaded source provided each register file (1 = yes). */
int         se_has_vdp1_registers(se_context* ctx);
int         se_has_vdp2_registers(se_context* ctx);
/* Register value by hardware byte offset (e.g. VDP2 0x0E = RAMCTL). Returns 0
   when the loaded source didn't provide that register file. */
uint16_t    se_get_vdp1_register(se_context* ctx, uint32_t hw_offset);
uint16_t    se_get_vdp2_register(se_context* ctx, uint32_t hw_offset);
/* Copy raw bytes from VDP1/VDP2 VRAM or CRAM (Saturn-native big-endian). Returns
   bytes copied (clamped to the region). */
size_t      se_read_vram(se_context* ctx, se_vram_kind kind, uint32_t offset,
                         void* dst, size_t size);
/* Write raw Saturn big-endian bytes into a memory region (currently work RAM
   only). Updates the current snapshot so the change is visible immediately, and
   forwards to the source's write_main_ram when present (SE_CAP_MEM_WRITE) so a
   live emulator is poked; on a savestate the edit is in-memory only. Returns the
   number of bytes written (0 if the region is not writable). */
size_t      se_write_vram(se_context* ctx, se_vram_kind kind, uint32_t offset,
                          const void* src, size_t size);
/* 1 when the current source has memory the Hex Editor can edit (a loaded
   snapshot). Edits always update the view; they persist to the emulator only when
   the source advertises SE_CAP_MEM_WRITE. */
int         se_can_write(se_context* ctx);
/* Decode CRAM entries [start, start+count) into palette entries. Returns the
   number written (clamped to the CRAM size for the current color mode). */
size_t      se_read_cram_colors(se_context* ctx, uint16_t start, uint16_t count,
                                se_palette_entry* out);
/* The CRAM color mode of the current snapshot (fixes entry width: RGB555 words
   vs RGB888 dwords). Returns SE_CRAM_RGB555_1024 when no data is loaded. */
se_cram_mode se_get_cram_mode(se_context* ctx);

/* --- ROM & Archive Search / Asset Trace (async; poll incrementally) --- */
typedef struct se_search* se_search_handle;
se_search_handle se_rom_search_begin(se_context* ctx, const se_search_query* q);
size_t           se_rom_search_poll (se_context* ctx, se_search_handle h,
                                     se_search_result* out, size_t max);
int              se_rom_search_done (se_context* ctx, se_search_handle h); /* 1 when finished */
void             se_rom_search_end  (se_context* ctx, se_search_handle h);

/* --- Reference Explorer --- */
size_t      se_references_of_texture(se_context* ctx, const se_texture_ref* ref,
                                     se_reference* out, size_t max);
size_t      se_references_of_palette(se_context* ctx, uint32_t clut_address,
                                     se_reference* out, size_t max);

/* --- Memory History --- */
size_t      se_history_for(se_context* ctx, uint32_t address,
                           se_mem_event* out, size_t max);

/* --- System status (status bar) --- */
se_result   se_get_system_status(se_context* ctx, se_system_status* out);

/* --- SH-2 registers (Assembly/debugger; requires SE_CAP_SH2_REGS). 'cpu' is
   se_sh2_cpu (0 master, 1 slave). Returns SE_OK and fills '*out' when available,
   SE_ERR_NO_DATA otherwise. --- */
se_result   se_get_sh2_regs(se_context* ctx, int cpu, se_sh2_regs* out);
int         se_has_sh2_regs(se_context* ctx);

/* --- SCSP voices / "Sound" panel (live sources only; requires SE_CAP_SCSP_SLOTS).
   se_scsp_slot_count returns how many voices the loaded source provided (0 or 32,
   0 when unavailable). se_get_scsp_slots copies up to SE_SCSP_SLOT_COUNT decoded
   voices into 'out' and returns the number written. --- */
int         se_scsp_slot_count(se_context* ctx);
int         se_get_scsp_slots(se_context* ctx, se_scsp_slot out[SE_SCSP_SLOT_COUNT]);

/* --- Live CD-block state / "Disc Explorer" (live sources only; requires SE_CAP_CD_STATUS).
   se_get_cd_status fills 'out' with the drive's current FAD + state and returns 1; 0 when
   unavailable (savestate / build without the CD tap). --- */
int         se_get_cd_status(se_context* ctx, se_cd_status* out);
/* Decode voice 'slot' (0..SE_SCSP_SLOT_COUNT-1) into 16-bit signed mono host PCM: reads the
   sample from sound RAM (SA..SA+LEA), converting 16-bit big-endian / 8-bit PCM. Writes up to
   'max_frames' samples into 'out'; returns the number written (0 if no sample / no sound RAM).
   '*out_sample_rate' (may be NULL) gets the voice's natural rate in Hz. Powers Play/Export. */
int         se_decode_scsp_sample(se_context* ctx, int slot, int16_t* out, int max_frames,
                                  uint32_t* out_sample_rate);

/* --- Frame control (live sources only; requires SE_CAP_FRAME_STEP) ---
   se_supports_frame_control returns 1 when the source can pause/step (so the
   host can enable those toolbar buttons). pause halts the emulator after the
   current frame; resume lets it free-run; step advances exactly 'frames' frames
   and leaves it paused; frame_number is the current emulated-frame counter.
   The control calls are asynchronous best-effort (they post to the live driver);
   the effect is visible on the next snapshot. They return SE_ERR_NO_CAPABILITY
   when the source doesn't support frame control. */
int         se_supports_frame_control(se_context* ctx);
se_result   se_frame_pause (se_context* ctx);
se_result   se_frame_resume(se_context* ctx);
se_result   se_frame_step  (se_context* ctx, int32_t frames);
uint64_t    se_frame_number(se_context* ctx);

/* --- Rewind / load-state (live sources only; requires SE_CAP_STATE_REWIND) ---
   se_supports_state_rewind returns 1 when the source can restore a full emulator
   savestate. se_load_state hands the emulator an opaque state image (reconstructed
   client-side from a keyframe+delta ring) and the frame number to adopt; the
   emulator restores at a frame boundary and stays paused. Best-effort/async (posts
   to the live driver); returns SE_ERR_NO_CAPABILITY when unsupported. */
int         se_supports_state_rewind(se_context* ctx);
se_result   se_load_state(se_context* ctx, uint64_t frame,
                          const void* state, size_t state_len,
                          const void* edits, size_t edits_len);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SATURNEXPLORER_SE_HOST_H */
