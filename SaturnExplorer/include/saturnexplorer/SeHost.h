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
 *
 * Exceptions: none. Every function here is no-throw, including under memory
 * pressure -- the host may be C, or JavaScript in the web build, so there is no
 * frame on the other side of the seam that could catch one, and unwinding past
 * it is undefined. A failed allocation surfaces as SE_ERR_NO_MEMORY from the
 * functions that return se_result, and as the same empty answer as "no data"
 * from those that return a count, a handle, or a flag.
 *
 * ROM/archive search and memory-history queries are deliberately absent. They
 * were declared here through ABI 5 and never implemented behind the seam; the
 * working implementations are frontend systems (FrontEnd/src/DataSearch.h,
 * FrontEnd/src/Debug) built on se_read_vram and the disc API, which is where
 * they belong -- both are interactive, cancellable, and hold their own state
 * across frames. Restoring them to the seam means designing that lifetime
 * first, not re-declaring the signatures.
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

/* A counter bumped every time the context re-derives what it caches from the snapshot --
   se_begin_frame, but also a scrub seek, se_write_vram and se_set_vdpN_register. Read it
   on two consecutive passes: an unchanged value means the snapshot has not moved between
   them, so anything derived from it is still good and an expensive query will be answered
   from cache. This exists because a client otherwise has to enumerate the paths that
   re-derive, which means re-deriving them by hand and being silently wrong when one is
   added. 0 when 'ctx' is null. */
uint64_t    se_derive_serial(se_context* ctx);

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

/* --- Per-layer viewers: a VDP2 scroll screen's tile data ---
       Together these describe the background a scroll screen draws: a tileset (the art),
       one index per map cell (the arrangement), and the shape of both. The layer image
       itself is just se_render_frame with that one layer enabled and
       se_render_opts::transparent_background set. --- */

/* Shape of a VDP2 scroll screen's tile map (see se_vdp2_tilemap), including tile_count.
   Counting distinct tiles means walking the screen's whole plane grid -- up to 512x512
   pattern-name decodes for RBG0 -- so the result is cached until the next se_begin_frame
   or in-place VRAM edit. On a live source that is every frame, so anything drawing every
   frame wants se_get_vdp2_tilemap_shape instead. */
se_result   se_get_vdp2_tilemap(se_context* ctx, se_vdp2_layer layer, se_vdp2_tilemap* out);

/* The same description without the grid walk: active, bitmap, cell_pixels, color_count,
   map_width and map_height all come from the VDP2 registers alone, so this costs a handful
   of register reads however large the map is. tile_count and truncated are always 0 --
   they are the two fields the walk exists to produce. Call se_get_vdp2_tilemap when the
   count is actually needed. */
se_result   se_get_vdp2_tilemap_shape(se_context* ctx, se_vdp2_layer layer,
                                      se_vdp2_tilemap* out);

/* The tile map itself: map_width * map_height indices, row-major, each an index into the
   tileset se_render_vdp2_tileset() draws. Writes at most 'max'; returns the number
   written (0 for a bitmap or disabled screen). */
size_t      se_get_vdp2_tile_indices(se_context* ctx, se_vdp2_layer layer,
                                     uint32_t* out, size_t max);

/* The screen's distinct character patterns, laid out as a grid 'columns' tiles wide
   (pass SE_VDP2_TILESET_COLUMNS for the layout the tile indices assume). Transparent
   texels come back with alpha 0. Same two-call size convention as se_render_frame;
   returns SE_ERR_NO_DATA when the screen has no tiles. */
se_result   se_render_vdp2_tileset(se_context* ctx, se_vdp2_layer layer, uint32_t columns,
                                   se_image* out, size_t* needed);

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

/* Overwrite one VDP register (16-bit, addressed by its hardware byte offset) in the loaded
 * snapshot; the reconstructed image re-derives so the edit shows immediately. Returns 1 if the
 * offset is within the captured register file, 0 otherwise. Snapshot-level edit (a visual
 * scratchpad) — it does not poke a running emulator. Additive: no ABI struct change. */
int         se_set_vdp1_register(se_context* ctx, uint32_t hw_offset, uint16_t value);
int         se_set_vdp2_register(se_context* ctx, uint32_t hw_offset, uint16_t value);
/* Copy raw bytes from VDP1/VDP2 VRAM or CRAM (Saturn-native big-endian). Returns
   bytes copied (clamped to the region). */
size_t      se_read_vram(se_context* ctx, se_vram_kind kind, uint32_t offset,
                         void* dst, size_t size);
/* Write raw Saturn big-endian bytes into a memory region. The source's write callback for
   the region (write_main_ram / write_sound_ram / write_vram, SE_CAP_MEM_WRITE) is asked first
   and the current snapshot takes only the bytes it accepted, so the view never shows an edit
   the source refused; a region the source has no callback for (a savestate) keeps the whole
   edit in-memory only. Returns the number of bytes accepted -- for a live source that means
   queued for the emulator, not yet applied -- and 0 if nothing was written. */
size_t      se_write_vram(se_context* ctx, se_vram_kind kind, uint32_t offset,
                          const void* src, size_t size);
/* 1 when the current source has memory the Hex Editor can edit (a loaded
   snapshot). Edits always update the view; they persist to the emulator only when
   the source advertises SE_CAP_MEM_WRITE. */
int         se_can_write(se_context* ctx);
/* 1 when an accepted se_write_vram to 'kind' is also handed to the source (a live emulator's poke
 * queue, a scrubbed frame's replay list), 0 when the edit changes only the loaded snapshot -- a
 * savestate or dump, or a source with no write callback for that region. Lets a caller say
 * "sent to the emulator" or "this view only" instead of calling both "modified". */
int         se_has_write_sink(se_context* ctx, se_vram_kind kind);
/* Decode CRAM entries [start, start+count) into palette entries. Returns the
   number written (clamped to the CRAM size for the current color mode). */
size_t      se_read_cram_colors(se_context* ctx, uint16_t start, uint16_t count,
                                se_palette_entry* out);
/* The CRAM color mode of the current snapshot (fixes entry width: RGB555 words
   vs RGB888 dwords). Returns SE_CRAM_RGB555_1024 when no data is loaded. */
se_cram_mode se_get_cram_mode(se_context* ctx);

/* --- Reference Explorer --- */
size_t      se_references_of_texture(se_context* ctx, const se_texture_ref* ref,
                                     se_reference* out, size_t max);
size_t      se_references_of_palette(se_context* ctx, uint32_t clut_address,
                                     se_reference* out, size_t max);

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
