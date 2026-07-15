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

#include "se_types.h"
#include "se_data_source.h"

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

/* --- VRAM Visualization --- */
size_t      se_vram_region_count(se_context* ctx);
se_result   se_get_vram_region  (se_context* ctx, size_t index, se_vram_region* out);

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

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SATURNEXPLORER_SE_HOST_H */
