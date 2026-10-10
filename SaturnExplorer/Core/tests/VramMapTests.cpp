// The VRAM Map's region list: command tables for every command, and shared blocks keeping
// their largest extent and every owner.

#include <iostream>
#include <vector>

#include "FakeVdpSource.h"
#include "saturnexplorer/SeHost.h"

namespace
{
using se_test::PutBE16;
using se_test::State;

int gFailures = 0;

void Check(bool condition, const char* expression, int line)
{
    if (condition) return;
    std::cerr << "CHECK failed at line " << line << ": " << expression << '\n';
    ++gFailures;
}
#define CHECK(expression) Check(static_cast<bool>(expression), #expression, __LINE__)

std::vector<se_vram_region> Regions(se_context* ctx)
{
    std::vector<se_vram_region> out;
    const size_t n = se_vram_region_count(ctx);
    for (size_t i = 0; i < n; ++i)
    {
        se_vram_region r {};
        if (se_get_vram_region(ctx, i, &r) == SE_OK) out.push_back(r);
    }
    return out;
}

int Count(const std::vector<se_vram_region>& rs, uint32_t addr, se_vram_region_kind kind)
{
    int n = 0;
    for (const auto& r : rs) if (r.address == addr && r.kind == kind) ++n;
    return n;
}

bool Owns(const std::vector<se_vram_region>& rs, uint32_t addr, se_vram_region_kind kind, uint32_t ref)
{
    for (const auto& r : rs)
        if (r.address == addr && r.kind == kind && r.ref_index == ref) return true;
    return false;
}

void EndAt(State& state, uint32_t cmd) { PutBE16(state.vdp1, cmd, 0x8000); }

// A gouraud-shaded polygon has a command table and a gouraud table, and no texture.
void PolygonsListTheirTables()
{
    State state(0x1000);
    se_test::WriteSystemClip(state, 160, 120);                 // command 0 at 0x00
    PutBE16(state.vdp1, 0x20, 0x0004);                         // command 1: polygon
    PutBE16(state.vdp1, 0x24, 0x0044);                         // CMDPMOD: gouraud, SPD
    PutBE16(state.vdp1, 0x3C, 0x0200 / 8);                     // CMDGRDA -> 0x200
    EndAt(state, 0x40);                                        // command 2: end
    se_context* ctx = se_test::CreateContext(state);
    if (ctx) se_begin_frame(ctx);
    CHECK(ctx != nullptr);
    if (!ctx) return;
    const auto rs = Regions(ctx);
    CHECK(Owns(rs, 0x20, SE_VRAM_CMD_TABLE, 1));
    CHECK(Owns(rs, 0x00, SE_VRAM_CMD_TABLE, 0));               // control commands too
    CHECK(Owns(rs, 0x200, SE_VRAM_GOURAUD, 1));
    for (const auto& r : rs)
    {
        if (r.address == 0x20 && r.kind == SE_VRAM_CMD_TABLE) CHECK(r.size == 0x20);
        if (r.address == 0x200 && r.kind == SE_VRAM_GOURAUD) CHECK(r.size == 0x08);
        CHECK(r.kind != SE_VRAM_TEXTURE);
    }
    se_destroy(ctx);
}

void Sprite4bpp(State& state, uint32_t cmd, uint32_t texture, int w, int h)
{
    PutBE16(state.vdp1, cmd + 0x00, 0x0000);                   // normal sprite
    PutBE16(state.vdp1, cmd + 0x04, 0x0040);                   // 4 bpp bank, SPD
    PutBE16(state.vdp1, cmd + 0x08, static_cast<uint16_t>(texture / 8));
    PutBE16(state.vdp1, cmd + 0x0A, static_cast<uint16_t>(((w / 8) << 8) | h));
}

// Two sprites on one texture: the block is as large as the larger sprite needs, and both
// sprites own it — whichever order they come in.
void SharedTexturesKeepTheirExtentAndOwners()
{
    for (int order = 0; order < 2; ++order)
    {
        State state(0x1000);
        se_test::WriteSystemClip(state, 160, 120);
        Sprite4bpp(state, 0x20, 0x400, order ? 32 : 8, order ? 8 : 1);
        Sprite4bpp(state, 0x40, 0x400, order ? 8 : 32, order ? 1 : 8);
        EndAt(state, 0x60);
        se_context* ctx = se_test::CreateContext(state);
        if (ctx) se_begin_frame(ctx);
        CHECK(ctx != nullptr);
        if (!ctx) return;
        const auto rs = Regions(ctx);
        CHECK(Owns(rs, 0x400, SE_VRAM_TEXTURE, 1));
        CHECK(Owns(rs, 0x400, SE_VRAM_TEXTURE, 2));
        CHECK(Count(rs, 0x400, SE_VRAM_TEXTURE) == 2);
        for (const auto& r : rs)
            if (r.address == 0x400 && r.kind == SE_VRAM_TEXTURE) CHECK(r.size == 128);   // 32x8 at 4 bpp
        se_destroy(ctx);
    }
}

// A command listed twice for the same block (e.g. one sprite's CLUT) is not duplicated.
void OwnersAreNotDuplicated()
{
    State state(0x1000);
    se_test::WriteSystemClip(state, 160, 120);
    Sprite4bpp(state, 0x20, 0x400, 8, 8);
    EndAt(state, 0x40);
    se_context* ctx = se_test::CreateContext(state);
    if (ctx) se_begin_frame(ctx);
    CHECK(ctx != nullptr);
    if (!ctx) return;
    CHECK(Count(Regions(ctx), 0x400, SE_VRAM_TEXTURE) == 1);
    se_destroy(ctx);
}
}  // namespace

// A LUT-mode palette needs the 32-byte CLUT in VDP1 VRAM, and CRAM for any entry that names a
// color bank (MSB clear; MSB set is a direct RGB555 color). Missing data is "no data", not a black
// palette: a real all-zero CLUT and an absent one must stay distinguishable.
se_context* Captured(State& st)
{
    se_context* ctx = se_test::CreateContext(st);
    if (ctx && se_begin_frame(ctx) != SE_OK) { se_destroy(ctx); return nullptr; }
    return ctx;
}

void PaletteNeedsItsMemory()
{
    se_palette pal {};
    {
        State st(0x40);
        se_data_source src = se_test::MakeSource(st);
        src.capabilities &= ~(SE_CAP_VDP1_VRAM | SE_CAP_VDP1_FB);
        src.read_vdp1_vram = nullptr;
        se_context* ctx = se_test::CreateContext(src);
        CHECK(ctx != nullptr);
        if (!ctx) return;
        CHECK(se_begin_frame(ctx) == SE_OK);
        CHECK(se_decode_palette(ctx, 0, &pal) == SE_ERR_NO_DATA);
        se_destroy(ctx);
    }
    {
        State st(0x40);
        se_context* ctx = Captured(st);
        CHECK(ctx != nullptr);
        if (!ctx) return;
        CHECK(se_decode_palette(ctx, 0x20, &pal) == SE_OK);          // exactly fits
        CHECK(pal.count == 16 && pal.entries[0].raw == 0);           // a real all-zero CLUT
        CHECK(se_decode_palette(ctx, 0x22, &pal) == SE_ERR_NO_DATA); // runs off the end
        CHECK(se_decode_palette(ctx, 0xFFFFFFF0u, &pal) == SE_ERR_NO_DATA);
        se_destroy(ctx);
    }
    {
        // Direct RGB entries need no CRAM.
        State st(0x40);
        for (uint32_t i = 0; i < 16; ++i) PutBE16(st.vdp1, 0x20 + i * 2, 0x801F);
        st.cram.clear();
        se_context* ctx = Captured(st);
        CHECK(ctx != nullptr);
        if (!ctx) return;
        CHECK(se_decode_palette(ctx, 0x20, &pal) == SE_OK);
        CHECK(pal.entries[0].r == 255 && pal.entries[0].g == 0 && pal.entries[0].b == 0);
        se_destroy(ctx);
    }
    {
        // A color-bank entry does.
        State st(0x40);
        for (uint32_t i = 0; i < 16; ++i) PutBE16(st.vdp1, 0x20 + i * 2, 0x801F);
        PutBE16(st.vdp1, 0x24, 0x0001);
        st.cram.clear();
        se_context* ctx = Captured(st);
        CHECK(ctx != nullptr);
        if (!ctx) return;
        CHECK(se_decode_palette(ctx, 0x20, &pal) == SE_ERR_NO_DATA);
        se_destroy(ctx);
    }
}

// The palette viewer and the texture decoder must agree on every entry: a 16x1 4bpp LUT texture
// whose pixel p is index p decodes to exactly the palette's entries.
void PaletteAgreesWithTextureDecode()
{
    State st(0x80);
    const uint16_t clut[16] = { 0x8000, 0x801F, 0x83E0, 0xFC00, 0x0001, 0x0002, 0x0010, 0x8421,
                                0x7FFF, 0x0000, 0x9999, 0x0123, 0xFFFF, 0x0040, 0x8001, 0x0005 };
    for (uint32_t i = 0; i < 16; ++i) PutBE16(st.vdp1, 0x20 + i * 2, clut[i]);
    for (uint32_t i = 0; i < 8; ++i) st.vdp1[0x40 + i] = uint8_t(((2 * i) << 4) | (2 * i + 1));
    for (uint32_t i = 0; i < 64; ++i) PutBE16(st.cram, i * 2, uint16_t(0x8000 | (i * 0x0421)));
    se_context* ctx = Captured(st);
    CHECK(ctx != nullptr);
    if (!ctx) return;
    se_palette pal {};
    CHECK(se_decode_palette(ctx, 0x20, &pal) == SE_OK);
    se_texture_ref ref {};
    ref.vram_address = 0x40; ref.width = 16; ref.height = 1;
    ref.color_mode = SE_COLOR_LUT_16; ref.clut_address = 0x20;
    std::vector<uint8_t> px(16 * 4);
    se_image img {};
    img.pixels = px.data(); img.capacity = px.size();
    size_t needed = 0;
    CHECK(se_decode_texture(ctx, &ref, &img, &needed) == SE_OK);
    for (uint32_t p = 1; p < 16; ++p)   // pixel 0 may be transparent
    {
        CHECK(px[p * 4 + 0] == pal.entries[p].r && px[p * 4 + 1] == pal.entries[p].g &&
              px[p * 4 + 2] == pal.entries[p].b);
    }
    se_destroy(ctx);
}

// A scaled sprite reports its on-screen size over its texture size, for each zoom-point mode
// the renderer draws, and a negative factor when it is drawn mirrored.
void ScaledSpriteReportsItsScale()
{
    struct Case { uint16_t zp; int16_t xa, ya, xb, yb, xc, yc; float sx, sy; };
    const Case cases[] = {
        { 0x0, 10, 10, 0, 0, 73, 41,  2.0f, 2.0f },   // two-point: A..C spans 64x32
        { 0x5, 10, 10, 15, 7, 0, 0,   0.5f, 0.5f },   // near-edge anchor: B is 16x8 inclusive
        { 0xA, 100, 100, 63, 31, 0, 0, 2.0f, 2.0f },  // centred anchor
        { 0x0, 50, 0, 0, 0, 19, 15,  -1.0f, 1.0f },   // C left of A: mirrored horizontally
    };
    for (const Case& c : cases)
    {
        State st(0x80);
        PutBE16(st.vdp1, 0x00, uint16_t(0x0001 | (c.zp << 8)));   // scaled sprite
        PutBE16(st.vdp1, 0x0A, 0x0410);                            // 32x16 texture
        PutBE16(st.vdp1, 0x0C, uint16_t(c.xa)); PutBE16(st.vdp1, 0x0E, uint16_t(c.ya));
        PutBE16(st.vdp1, 0x10, uint16_t(c.xb)); PutBE16(st.vdp1, 0x12, uint16_t(c.yb));
        PutBE16(st.vdp1, 0x14, uint16_t(c.xc)); PutBE16(st.vdp1, 0x16, uint16_t(c.yc));
        PutBE16(st.vdp1, 0x20, 0x8000);                            // END
        se_context* ctx = se_test::CreateContext(st);
        CHECK(ctx != nullptr);
        if (!ctx) return;
        CHECK(se_begin_frame(ctx) == SE_OK);
        se_command cmd {};
        CHECK(se_get_command(ctx, 0, &cmd) == SE_OK);
        CHECK(cmd.type == SE_CMD_SCALED_SPRITE);
        CHECK(cmd.scale_x == c.sx && cmd.scale_y == c.sy);
        se_destroy(ctx);
    }

    // A normal sprite is unscaled.
    State st(0x80);
    PutBE16(st.vdp1, 0x0A, 0x0410);
    PutBE16(st.vdp1, 0x20, 0x8000);
    se_context* ctx = se_test::CreateContext(st);
    CHECK(ctx != nullptr);
    if (!ctx) return;
    CHECK(se_begin_frame(ctx) == SE_OK);
    se_command cmd {};
    CHECK(se_get_command(ctx, 0, &cmd) == SE_OK);
    CHECK(cmd.scale_x == 1.0f && cmd.scale_y == 1.0f);
    se_destroy(ctx);
}

int main()
{
    PolygonsListTheirTables();
    SharedTexturesKeepTheirExtentAndOwners();
    OwnersAreNotDuplicated();
    PaletteNeedsItsMemory();
    PaletteAgreesWithTextureDecode();
    ScaledSpriteReportsItsScale();
    if (gFailures) { std::cerr << gFailures << " failure(s)\n"; return 1; }
    std::cout << "VramMap tests passed\n";
    return 0;
}
