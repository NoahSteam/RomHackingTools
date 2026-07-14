// host_abi.cpp — the C ABI shim for Seam B. Each exported extern "C" function
// casts the opaque se_context* to the C++ se::Context and delegates. No C++
// types cross the boundary. M1 leaves most queries as SE_ERR_UNIMPLEMENTED
// stubs; the lifecycle + frame snapshot path is real and exercises Seam A.

#include <new>

#include "saturnexplorer/se_host.h"
#include "context.h"

namespace {
inline se::Context* Impl(se_context* c) { return reinterpret_cast<se::Context*>(c); }
}  // namespace

extern "C" {

uint32_t se_abi_version(void) { return SE_ABI_VERSION; }

se_context* se_create(const se_data_source* ds, const se_config* cfg) {
    if (!ds || !cfg) return nullptr;
    if (ds->abi_version != SE_ABI_VERSION) return nullptr;
    if (cfg->abi_version != SE_ABI_VERSION) return nullptr;
    return reinterpret_cast<se_context*>(new (std::nothrow) se::Context(*ds, *cfg));
}

void se_destroy(se_context* ctx) { delete Impl(ctx); }

se_result se_begin_frame(se_context* ctx) {
    if (!ctx) return SE_ERR_INVALID_ARG;
    return Impl(ctx)->BeginFrame();
}

/* --- Command Table / Sprite Inspection --- */
size_t se_command_count(se_context* ctx) {
    return ctx ? Impl(ctx)->CommandCount() : 0;
}
se_result se_get_command(se_context* ctx, size_t index, se_command* out) {
    (void)index;
    if (!ctx || !out) return SE_ERR_INVALID_ARG;
    return SE_ERR_UNIMPLEMENTED;
}
se_result se_hit_test(se_context* ctx, int x, int y, size_t* out_index) {
    (void)x; (void)y;
    if (!ctx || !out_index) return SE_ERR_INVALID_ARG;
    return SE_ERR_UNIMPLEMENTED;
}

/* --- VDP1 geometry --- */
size_t se_sprite_count(se_context* ctx) {
    return ctx ? Impl(ctx)->SpriteCount() : 0;
}
se_result se_get_sprite_2d(se_context* ctx, size_t index, se_sprite_2d* out) {
    (void)index;
    if (!ctx || !out) return SE_ERR_INVALID_ARG;
    return SE_ERR_UNIMPLEMENTED;
}
se_result se_get_sprite_3d(se_context* ctx, size_t index, se_sprite_3d* out) {
    (void)index;
    if (!ctx || !out) return SE_ERR_INVALID_ARG;
    return SE_ERR_UNIMPLEMENTED;
}

/* --- Software composite --- */
se_result se_render_frame(se_context* ctx, const se_render_opts* opts,
                          se_image* out, size_t* needed) {
    (void)opts;
    if (!ctx || !out) return SE_ERR_INVALID_ARG;
    if (needed) *needed = 0;
    return SE_ERR_UNIMPLEMENTED;
}

/* --- Texture & Palette --- */
se_result se_decode_texture(se_context* ctx, const se_texture_ref* ref,
                            se_image* out, size_t* needed) {
    if (!ctx || !ref || !out) return SE_ERR_INVALID_ARG;
    if (needed) *needed = 0;
    return SE_ERR_UNIMPLEMENTED;
}
se_result se_decode_palette(se_context* ctx, uint32_t clut_address, se_palette* out) {
    (void)clut_address;
    if (!ctx || !out) return SE_ERR_INVALID_ARG;
    return SE_ERR_UNIMPLEMENTED;
}

/* --- VRAM map --- */
size_t se_vram_region_count(se_context* ctx) {
    return ctx ? Impl(ctx)->VramRegionCount() : 0;
}
se_result se_get_vram_region(se_context* ctx, size_t index, se_vram_region* out) {
    (void)index;
    if (!ctx || !out) return SE_ERR_INVALID_ARG;
    return SE_ERR_UNIMPLEMENTED;
}

/* --- ROM & Archive Search --- */
se_search_handle se_rom_search_begin(se_context* ctx, const se_search_query* q) {
    (void)ctx; (void)q;
    return nullptr;  // no active search in M1
}
size_t se_rom_search_poll(se_context* ctx, se_search_handle h,
                          se_search_result* out, size_t max) {
    (void)ctx; (void)h; (void)out; (void)max;
    return 0;
}
int se_rom_search_done(se_context* ctx, se_search_handle h) {
    (void)ctx; (void)h;
    return 1;  // trivially finished
}
void se_rom_search_end(se_context* ctx, se_search_handle h) {
    (void)ctx; (void)h;
}

/* --- Reference Explorer --- */
size_t se_references_of_texture(se_context* ctx, const se_texture_ref* ref,
                                se_reference* out, size_t max) {
    (void)ctx; (void)ref; (void)out; (void)max;
    return 0;
}
size_t se_references_of_palette(se_context* ctx, uint32_t clut_address,
                                se_reference* out, size_t max) {
    (void)ctx; (void)clut_address; (void)out; (void)max;
    return 0;
}

/* --- Memory History --- */
size_t se_history_for(se_context* ctx, uint32_t address,
                      se_mem_event* out, size_t max) {
    (void)ctx; (void)address; (void)out; (void)max;
    return 0;
}

/* --- System status --- */
se_result se_get_system_status(se_context* ctx, se_system_status* out) {
    if (!ctx || !out) return SE_ERR_INVALID_ARG;
    const se_data_source& ds = Impl(ctx)->DataSource();
    if ((ds.capabilities & SE_CAP_SYSTEM_STATUS) && ds.get_system_status) {
        return ds.get_system_status(ds.user, out) == 0 ? SE_OK : SE_ERR_IO;
    }
    return SE_ERR_NO_CAPABILITY;
}

}  // extern "C"
