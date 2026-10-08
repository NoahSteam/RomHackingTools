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

int main()
{
    PolygonsListTheirTables();
    SharedTexturesKeepTheirExtentAndOwners();
    OwnersAreNotDuplicated();
    if (gFailures) { std::cerr << gFailures << " failure(s)\n"; return 1; }
    std::cout << "VramMap tests passed\n";
    return 0;
}
