// HostAbi.cpp — the C ABI shim for Seam B. Each exported extern "C" function
// casts the opaque se_context* to the C++ se::Context and delegates. No C++
// types cross the boundary. M1 leaves most queries as SE_ERR_UNIMPLEMENTED
// stubs; the lifecycle + frame snapshot path is real and exercises Seam A.

#include <new>

#include "saturnexplorer/SeHost.h"
#include "Context.h"

namespace
{
inline se::Context* Impl(se_context* c)
{
    return reinterpret_cast<se::Context*>(c);
}
}  // namespace

extern "C" {

uint32_t se_abi_version(void)
{
    return SE_ABI_VERSION;
}

se_context* se_create(const se_data_source* ds, const se_config* cfg)
{
    if (!ds || !cfg)
    {
        return nullptr;
    }
    if (ds->abi_version != SE_ABI_VERSION || cfg->abi_version != SE_ABI_VERSION)
    {
        return nullptr;
    }
    return reinterpret_cast<se_context*>(new (std::nothrow) se::Context(*ds, *cfg));
}

void se_destroy(se_context* ctx)
{
    delete Impl(ctx);
}

se_result se_begin_frame(se_context* ctx)
{
    if (!ctx)
    {
        return SE_ERR_INVALID_ARG;
    }
    return Impl(ctx)->BeginFrame();
}

/* --- Command Table / Sprite Inspection --- */
size_t se_command_count(se_context* ctx)
{
    return ctx ? Impl(ctx)->CommandCount() : 0;
}

se_result se_get_command(se_context* ctx, size_t index, se_command* out)
{
    if (!ctx || !out)
    {
        return SE_ERR_INVALID_ARG;
    }
    return Impl(ctx)->GetCommand(index, out);
}

se_result se_hit_test(se_context* ctx, int x, int y, size_t* out_index)
{
    if (!ctx || !out_index)
    {
        return SE_ERR_INVALID_ARG;
    }
    return Impl(ctx)->HitTest(x, y, out_index);
}

se_result se_hit_test_3d(se_context* ctx, const se_camera3d* camera,
                         int x, int y, size_t* out_index)
{
    if (!ctx || !camera || !out_index)
    {
        return SE_ERR_INVALID_ARG;
    }
    return Impl(ctx)->HitTest3D(*camera, x, y, out_index);
}

/* --- VDP1 geometry --- */
size_t se_sprite_count(se_context* ctx)
{
    return ctx ? Impl(ctx)->SpriteCount() : 0;
}

se_result se_get_sprite_2d(se_context* ctx, size_t index, se_sprite_2d* out)
{
    if (!ctx || !out)
    {
        return SE_ERR_INVALID_ARG;
    }
    return Impl(ctx)->GetSprite2d(index, out);
}

se_result se_get_sprite_3d(se_context* ctx, size_t index, se_sprite_3d* out)
{
    if (!ctx || !out)
    {
        return SE_ERR_INVALID_ARG;
    }
    return Impl(ctx)->GetSprite3d(index, out);
}

/* --- Software composite --- */
se_result se_render_frame(se_context* ctx, const se_render_opts* opts,
                          se_image* out, size_t* needed)
{
    if (!ctx || !opts || !out)
    {
        return SE_ERR_INVALID_ARG;
    }
    return Impl(ctx)->RenderFrame(*opts, out, needed);
}

se_result se_render_3d(se_context* ctx, const se_camera3d* camera,
                       const se_render_opts* opts, se_image* out, size_t* needed)
{
    if (!ctx || !camera || !opts || !out)
    {
        return SE_ERR_INVALID_ARG;
    }
    return Impl(ctx)->Render3D(*camera, *opts, out, needed);
}

/* --- Texture & Palette --- */
se_result se_decode_texture(se_context* ctx, const se_texture_ref* ref,
                            se_image* out, size_t* needed)
{
    if (!ctx || !ref || !out)
    {
        return SE_ERR_INVALID_ARG;
    }
    return Impl(ctx)->DecodeTexture(*ref, out, needed);
}

se_result se_decode_palette(se_context* ctx, uint32_t clut_address, se_palette* out)
{
    if (!ctx || !out)
    {
        return SE_ERR_INVALID_ARG;
    }
    return Impl(ctx)->DecodePalette(clut_address, out);
}

se_result se_decode_bank_palette(se_context* ctx, uint16_t color_bank,
                                 se_color_mode color_mode, se_palette* out)
{
    if (!ctx || !out)
    {
        return SE_ERR_INVALID_ARG;
    }
    return Impl(ctx)->DecodeBankPalette(color_bank, color_mode, out);
}

/* --- VRAM map --- */
size_t se_vram_region_count(se_context* ctx)
{
    return ctx ? Impl(ctx)->VramRegionCount() : 0;
}

se_result se_get_vram_region(se_context* ctx, size_t index, se_vram_region* out)
{
    if (!ctx || !out)
    {
        return SE_ERR_INVALID_ARG;
    }
    return Impl(ctx)->GetVramRegion(index, out);
}

int se_has_vdp1_registers(se_context* ctx)
{
    return (ctx && Impl(ctx)->HasVdp1Regs()) ? 1 : 0;
}

int se_has_vdp2_registers(se_context* ctx)
{
    return (ctx && Impl(ctx)->HasVdp2Regs()) ? 1 : 0;
}

uint16_t se_get_vdp1_register(se_context* ctx, uint32_t hw_offset)
{
    return ctx ? Impl(ctx)->Vdp1Register(hw_offset) : 0;
}

uint16_t se_get_vdp2_register(se_context* ctx, uint32_t hw_offset)
{
    return ctx ? Impl(ctx)->Vdp2Register(hw_offset) : 0;
}

size_t se_read_vram(se_context* ctx, se_vram_kind kind, uint32_t offset,
                    void* dst, size_t size)
{
    return ctx ? Impl(ctx)->ReadVram(kind, offset, dst, size) : 0;
}

size_t se_write_vram(se_context* ctx, se_vram_kind kind, uint32_t offset,
                     const void* src, size_t size)
{
    return ctx ? Impl(ctx)->WriteVram(kind, offset, src, size) : 0;
}

int se_can_write(se_context* ctx)
{
    return (ctx && Impl(ctx)->CanWrite()) ? 1 : 0;
}

size_t se_read_cram_colors(se_context* ctx, uint16_t start, uint16_t count,
                           se_palette_entry* out)
{
    return ctx ? Impl(ctx)->ReadCramColors(start, count, out) : 0;
}

se_cram_mode se_get_cram_mode(se_context* ctx)
{
    return ctx ? Impl(ctx)->CramMode() : SE_CRAM_RGB555_1024;
}

/* --- ROM & Archive Search --- */
se_search_handle se_rom_search_begin(se_context* ctx, const se_search_query* q)
{
    (void)ctx;
    (void)q;
    return nullptr;  // no active search in M1
}

size_t se_rom_search_poll(se_context* ctx, se_search_handle h,
                          se_search_result* out, size_t max)
{
    (void)ctx;
    (void)h;
    (void)out;
    (void)max;
    return 0;
}

int se_rom_search_done(se_context* ctx, se_search_handle h)
{
    (void)ctx;
    (void)h;
    return 1;  // trivially finished
}

void se_rom_search_end(se_context* ctx, se_search_handle h)
{
    (void)ctx;
    (void)h;
}

/* --- Reference Explorer --- */
size_t se_references_of_texture(se_context* ctx, const se_texture_ref* ref,
                                se_reference* out, size_t max)
{
    if (!ctx || !ref)
    {
        return 0;
    }
    return Impl(ctx)->ReferencesOfTexture(*ref, out, max);
}

size_t se_references_of_palette(se_context* ctx, uint32_t clut_address,
                                se_reference* out, size_t max)
{
    if (!ctx)
    {
        return 0;
    }
    return Impl(ctx)->ReferencesOfPalette(clut_address, out, max);
}

/* --- Memory History --- */
size_t se_history_for(se_context* ctx, uint32_t address,
                      se_mem_event* out, size_t max)
{
    (void)ctx;
    (void)address;
    (void)out;
    (void)max;
    return 0;
}

/* --- System status --- */
se_result se_get_system_status(se_context* ctx, se_system_status* out)
{
    if (!ctx || !out)
    {
        return SE_ERR_INVALID_ARG;
    }

    const se_data_source& ds = Impl(ctx)->DataSource();
    if ((ds.capabilities & SE_CAP_SYSTEM_STATUS) && ds.get_system_status)
    {
        return ds.get_system_status(ds.user, out) == 0 ? SE_OK : SE_ERR_IO;
    }
    return SE_ERR_NO_CAPABILITY;
}

/* --- SH-2 registers --- */
se_result se_get_sh2_regs(se_context* ctx, int cpu, se_sh2_regs* out)
{
    if (!ctx || !out || cpu < 0 || cpu > 1)
    {
        return SE_ERR_INVALID_ARG;
    }
    return Impl(ctx)->GetSh2Regs(cpu, out);
}

int se_has_sh2_regs(se_context* ctx)
{
    if (!ctx)
    {
        return 0;
    }
    return (Impl(ctx)->HasSh2Regs(0) || Impl(ctx)->HasSh2Regs(1)) ? 1 : 0;
}

int se_scsp_slot_count(se_context* ctx)
{
    return ctx ? Impl(ctx)->ScspSlotCount() : 0;
}

int se_get_scsp_slots(se_context* ctx, se_scsp_slot out[SE_SCSP_SLOT_COUNT])
{
    if (!ctx || !out)
    {
        return 0;
    }
    return Impl(ctx)->GetScspSlots(out);
}

int se_decode_scsp_sample(se_context* ctx, int slot, int16_t* out, int max_frames,
                          uint32_t* out_sample_rate)
{
    if (!ctx || !out)
    {
        return 0;
    }
    return Impl(ctx)->DecodeScspSample(slot, out, max_frames, out_sample_rate);
}

/* --- Frame control --- */
int se_supports_frame_control(se_context* ctx)
{
    if (!ctx)
    {
        return 0;
    }
    const se_data_source& ds = Impl(ctx)->DataSource();
    return (ds.capabilities & SE_CAP_FRAME_STEP) && ds.frame_pause && ds.frame_step
               ? 1
               : 0;
}

se_result se_frame_pause(se_context* ctx)
{
    if (!ctx)
    {
        return SE_ERR_INVALID_ARG;
    }
    const se_data_source& ds = Impl(ctx)->DataSource();
    if (!(ds.capabilities & SE_CAP_FRAME_STEP) || !ds.frame_pause)
    {
        return SE_ERR_NO_CAPABILITY;
    }
    return ds.frame_pause(ds.user) == 0 ? SE_OK : SE_ERR_IO;
}

se_result se_frame_resume(se_context* ctx)
{
    if (!ctx)
    {
        return SE_ERR_INVALID_ARG;
    }
    const se_data_source& ds = Impl(ctx)->DataSource();
    if (!(ds.capabilities & SE_CAP_FRAME_STEP) || !ds.frame_step)
    {
        return SE_ERR_NO_CAPABILITY;
    }
    /* By the seam's contract, stepping <= 0 frames means "run free" (resume). */
    return ds.frame_step(ds.user, 0) == 0 ? SE_OK : SE_ERR_IO;
}

se_result se_frame_step(se_context* ctx, int32_t frames)
{
    if (!ctx)
    {
        return SE_ERR_INVALID_ARG;
    }
    const se_data_source& ds = Impl(ctx)->DataSource();
    if (!(ds.capabilities & SE_CAP_FRAME_STEP) || !ds.frame_step)
    {
        return SE_ERR_NO_CAPABILITY;
    }
    if (frames < 1)
    {
        frames = 1;
    }
    return ds.frame_step(ds.user, frames) == 0 ? SE_OK : SE_ERR_IO;
}

uint64_t se_frame_number(se_context* ctx)
{
    if (!ctx)
    {
        return 0;
    }
    const se_data_source& ds = Impl(ctx)->DataSource();
    if ((ds.capabilities & SE_CAP_FRAME_STEP) && ds.frame_number)
    {
        return ds.frame_number(ds.user);
    }
    return 0;
}

}  // extern "C"
