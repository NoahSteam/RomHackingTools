// Layer export tests — the per-layer viewer panels' data path, with no renderer.
//
// Everything the panels write comes from the core through LayerExport.h, so a synthetic
// VDP2 snapshot is enough to pin the whole chain: which layer pixels the isolated render
// produces, what the tile map says, which tiles the tileset image contains and in what
// order, the exact bytes of the tile-index CSV, and the names and dimensions of the files
// an export would write.
//
// The fixture is a 32x8 frame (set by a VDP1 system clip, as the compositor tests do) with
// NBG3 showing four solid-colour 8x8 characters repeating across the map — so the frame is
// exactly four tiles wide and one tall, and tile identity, tile order, tile boundaries and
// pixel decode are all individually observable.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "saturnexplorer/SeHost.h"

#include "FakeVdpSource.h"

#include "BinaryWriter.h"
#include "LayerExport.h"

using namespace sfe;

namespace
{
int gFailures = 0;

void Check(bool condition, const char* expression, int line)
{
    if (condition) return;
    std::cerr << "CHECK failed at line " << line << ": " << expression << '\n';
    ++gFailures;
}

#define CHECK(expression) Check(static_cast<bool>(expression), #expression, __LINE__)

// --- Synthetic Saturn state, on the shared Core test mock ---

using se_test::State;
using se_test::PutBE16;
using se_test::SetReg;

// The four cell colours, as CRAM entries and as the RGBA the renderer must produce.
const uint16_t kCram555[4] = { 0x7FFF, 0x001F, 0x03E0, 0x7C00 };
const uint8_t  kRgb[4][3]  = { { 255, 255, 255 }, { 255, 0, 0 }, { 0, 255, 0 }, { 0, 0, 255 } };

// se_test::CreateContext deliberately stops short of a snapshot, because a Core test
// pokes VRAM without one. Every test here renders, so take the snapshot too.
se_context* Open(State& state)
{
    se_context* ctx = se_test::CreateContext(state);
    CHECK(ctx != nullptr);
    CHECK(se_begin_frame(ctx) == SE_OK);
    return ctx;
}

State MakeTiledNbg3State()
{
    State state(0x200);   // room for the command table plus the sprite texture
    // A 32x8 VDP1 system clip establishes the composited frame dimensions: four 8x8
    // characters across, one down.
    se_test::WriteSystemClip(state, 32, 8);
    PutBE16(state.vdp1, 0x20, 0x8000);   // draw-end terminator

    SetReg(state, 0x020, 0x0008);   // BGON: NBG3 on
    SetReg(state, 0x036, 0x8000);   // PNCN3: one-word pattern names
    SetReg(state, 0x04C, 0x0101);   // MPABN3: planes A and B -> map number 1
    SetReg(state, 0x04E, 0x0101);   // MPCDN3: planes C and D -> map number 1
    SetReg(state, 0x0FA, 0x0100);   // PRINB: NBG3 priority 1

    // One page of pattern names at byte 0x2000 (map number 1, one-word 8x8 patterns).
    for (uint32_t py = 0; py < 64; ++py)
        for (uint32_t px = 0; px < 64; ++px)
            PutBE16(state.vdp2, 0x2000 + (py * 64 + px) * 2, static_cast<uint16_t>(1 + px % 4));

    // Characters 1-4: 32 bytes each of 4bpp, every nibble the character's own index, so
    // each tile is a solid block of CRAM colour 1-4.
    for (uint8_t ch = 1; ch <= 4; ++ch)
    {
        const uint8_t packed = static_cast<uint8_t>((ch << 4) | ch);
        std::fill(state.vdp2.begin() + ch * 0x20, state.vdp2.begin() + ch * 0x20 + 0x20, packed);
        PutBE16(state.cram, ch * 2, kCram555[ch - 1]);
    }
    return state;
}

// The same state plus one 8x2 opaque RGB555 VDP1 sprite covering the frame.
State MakeSpriteState()
{
    State state = MakeTiledNbg3State();
    PutBE16(state.vdp1, 0x20, 0x0000);          // CMDCTRL: normal sprite, link to next
    PutBE16(state.vdp1, 0x24, 0x0048);          // CMDPMOD: RGB555, SPD
    PutBE16(state.vdp1, 0x28, 0x100 / 8);       // CMDSRCA: texture at byte 0x100
    PutBE16(state.vdp1, 0x2A, (1 << 8) | 2);    // CMDSIZE: 8 wide, 2 tall
    PutBE16(state.vdp1, 0x40, 0x8000);          // draw-end terminator
    for (uint32_t i = 0; i < 16; ++i)
        PutBE16(state.vdp1, 0x100 + i * 2, 0xFFFF);
    return state;
}

se_render_opts AllLayers()
{
    se_render_opts opts = {};
    for (int i = 0; i < SE_LAYER_COUNT; ++i) opts.show_layer[i] = 1;
    opts.show_vdp1_sprites = 1;
    opts.highlight_command = -1;
    return opts;
}

// Render one layer in isolation — the composite with only that layer enabled and no
// backdrop under it, which is exactly what a panel draws.
std::vector<uint8_t> RenderLayer(se_context* ctx, LayerId layer, bool tileGrid,
                                 uint32_t& w, uint32_t& h)
{
    const se_render_opts opts = LayerRenderOpts(AllLayers(), layer, tileGrid);
    std::vector<uint8_t> px;
    CHECK(FetchCoreImage([&](se_image* i, size_t* n) { return se_render_frame(ctx, &opts, i, n); },
                         px, w, h));
    return px;
}

// Whether two images agree at one pixel.
bool SamePixel(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b,
               uint32_t stride, int x, int y)
{
    const size_t o = (static_cast<size_t>(y) * stride + x) * 4;
    return a[o] == b[o] && a[o + 1] == b[o + 1] && a[o + 2] == b[o + 2] && a[o + 3] == b[o + 3];
}

bool IsRgba(const std::vector<uint8_t>& px, uint32_t stride, int x, int y,
            uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    const size_t o = (static_cast<size_t>(y) * stride + x) * 4;
    return px[o] == r && px[o + 1] == g && px[o + 2] == b && px[o + 3] == a;
}

uint32_t ReadLE32(const std::vector<uint8_t>& v, size_t o)
{
    return v[o] | (v[o + 1] << 8) | (v[o + 2] << 16) | (static_cast<uint32_t>(v[o + 3]) << 24);
}

// A BMP's declared width / height / bits-per-pixel, straight out of its header.
void BmpHeader(const std::vector<uint8_t>& bmp, uint32_t& w, uint32_t& h, uint16_t& bpp)
{
    w = ReadLE32(bmp, 18);
    h = ReadLE32(bmp, 22);
    bpp = static_cast<uint16_t>(bmp[28] | (bmp[29] << 8));
}

const ExportFile* FindFile(const LayerExport& ex, const std::string& name)
{
    for (size_t i = 0; i < ex.files.size(); ++i)
        if (ex.files[i].name == name) return &ex.files[i];
    return nullptr;
}

// --- Tests ---

void TestIsolatedLayerRender()
{
    State state = MakeTiledNbg3State();
    se_context* ctx = Open(state);

    uint32_t w = 0;
    uint32_t h = 0;
    const std::vector<uint8_t> nbg3 = RenderLayer(ctx, kLayerNbg3, false, w, h);
    CHECK(w == 32);
    CHECK(h == 8);
    // The four characters repeat across X, so the frame reads as four solid 8-pixel
    // columns: white, red, green, blue.
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 32; ++x)
        {
            const uint8_t* c = kRgb[(x / 8) % 4];
            CHECK(IsRgba(nbg3, w, x, y, c[0], c[1], c[2], 255));
        }

    // A layer that draws nothing comes back fully transparent — no back screen, no
    // backdrop. That is what makes the panel's checkerboard meaningful.
    const std::vector<uint8_t> nbg0 = RenderLayer(ctx, kLayerNbg0, false, w, h);
    for (size_t i = 0; i < nbg0.size(); i += 4) CHECK(nbg0[i + 3] == 0);

    se_destroy(ctx);
}

void TestTileMapAndTileset()
{
    State state = MakeTiledNbg3State();
    se_context* ctx = Open(state);

    se_vdp2_tilemap info = {};
    CHECK(se_get_vdp2_tilemap(ctx, SE_LAYER_NBG3, &info) == SE_OK);
    CHECK(info.active == 1);
    CHECK(info.bitmap == 0);
    CHECK(info.truncated == 0);
    CHECK(info.cell_pixels == 8);
    CHECK(info.color_count == 16);
    // 2x2 planes of one 64x64-pattern page each.
    CHECK(info.map_width == 128);
    CHECK(info.map_height == 128);
    CHECK(info.tile_count == 4);

    std::vector<uint32_t> indices(static_cast<size_t>(info.map_width) * info.map_height, 99);
    CHECK(se_get_vdp2_tile_indices(ctx, SE_LAYER_NBG3, indices.data(), indices.size()) ==
          indices.size());
    // Tiles are numbered in first-seen (row-major) order, so cell (x, y) is tile x % 4.
    for (uint32_t y = 0; y < info.map_height; ++y)
        for (uint32_t x = 0; x < info.map_width; ++x)
            CHECK(indices[y * info.map_width + x] == x % 4);

    // The tileset is one row of four 8x8 tiles in a SE_VDP2_TILESET_COLUMNS-wide grid.
    se_image img = {};
    size_t needed = 0;
    CHECK(se_render_vdp2_tileset(ctx, SE_LAYER_NBG3, SE_VDP2_TILESET_COLUMNS, &img, &needed) == SE_OK);
    std::vector<uint8_t> tiles(needed);
    img.pixels = tiles.data();
    img.capacity = tiles.size();
    CHECK(se_render_vdp2_tileset(ctx, SE_LAYER_NBG3, SE_VDP2_TILESET_COLUMNS, &img, &needed) == SE_OK);
    CHECK(img.width == SE_VDP2_TILESET_COLUMNS * 8);
    CHECK(img.height == 8);
    for (int t = 0; t < 4; ++t)
        CHECK(IsRgba(tiles, img.width, t * 8 + 3, 3, kRgb[t][0], kRgb[t][1], kRgb[t][2], 255));
    // Past the last tile the grid is empty, not black.
    CHECK(IsRgba(tiles, img.width, 4 * 8 + 3, 3, 0, 0, 0, 0));

    se_destroy(ctx);
}

// The shape query must agree with the counting one on every register-derived field, and
// must not walk the grid to get there. It reports tile_count only when the full map is
// already cached, which is what lets a panel toolbar ask every frame on a live source.
void TestTileMapShapeMatchesWithoutCounting()
{
    State state = MakeTiledNbg3State();
    se_context* ctx = Open(state);

    se_vdp2_tilemap shape = {};
    CHECK(se_get_vdp2_tilemap_shape(ctx, SE_LAYER_NBG3, &shape) == SE_OK);
    CHECK(shape.active == 1);
    CHECK(shape.bitmap == 0);
    CHECK(shape.cell_pixels == 8);
    CHECK(shape.color_count == 16);
    CHECK(shape.map_width == 128);
    CHECK(shape.map_height == 128);
    // Nothing has counted yet, and an active non-bitmap screen always has at least one
    // tile — so a zero here is unambiguously "not counted" rather than "no tiles".
    CHECK(shape.tile_count == 0);
    CHECK(shape.truncated == 0);

    // Every register-derived field matches what the counting query reports.
    se_vdp2_tilemap full = {};
    CHECK(se_get_vdp2_tilemap(ctx, SE_LAYER_NBG3, &full) == SE_OK);
    CHECK(full.tile_count == 4);
    CHECK(shape.active == full.active);
    CHECK(shape.bitmap == full.bitmap);
    CHECK(shape.cell_pixels == full.cell_pixels);
    CHECK(shape.color_count == full.color_count);
    CHECK(shape.map_width == full.map_width);
    CHECK(shape.map_height == full.map_height);

    // Now that the map is cached, the shape query passes the count through rather than
    // rebuilding it — this is what makes the header line reappear once a frame holds still.
    se_vdp2_tilemap warm = {};
    CHECK(se_get_vdp2_tilemap_shape(ctx, SE_LAYER_NBG3, &warm) == SE_OK);
    CHECK(warm.tile_count == 4);

    // A re-derive drops the cache, so the shape query is back to not counting.
    CHECK(se_begin_frame(ctx) == SE_OK);
    se_vdp2_tilemap cold = {};
    CHECK(se_get_vdp2_tilemap_shape(ctx, SE_LAYER_NBG3, &cold) == SE_OK);
    CHECK(cold.map_width == 128);
    CHECK(cold.tile_count == 0);

    // A bitmap screen has no tile map at all; both queries must agree on that.
    se_destroy(ctx);
    State bmp = MakeTiledNbg3State();
    SetReg(bmp, 0x020, 0x0009);
    SetReg(bmp, 0x028, 0x0002);
    SetReg(bmp, 0x0F8, 0x0001);
    se_context* bctx = Open(bmp);
    se_vdp2_tilemap bshape = {};
    CHECK(se_get_vdp2_tilemap_shape(bctx, SE_LAYER_NBG0, &bshape) == SE_OK);
    CHECK(bshape.active == 1);
    CHECK(bshape.bitmap == 1);
    CHECK(bshape.map_width == 0);
    se_destroy(bctx);
}

// Rebuilding into a map that already holds a bigger result must not leave the previous
// one's tail behind: Reset keeps the containers' capacity, so the fields have to be
// cleared explicitly.
void TestRebuildDoesNotLeakPreviousMap()
{
    State state = MakeTiledNbg3State();
    se_context* ctx = Open(state);
    se_vdp2_tilemap big = {};
    CHECK(se_get_vdp2_tilemap(ctx, SE_LAYER_NBG3, &big) == SE_OK);
    CHECK(big.tile_count == 4);
    CHECK(big.map_width == 128);

    // Turn the screen off and re-derive: the same cache slot must come back empty.
    CHECK(se_set_vdp2_register(ctx, 0x020, 0x0000) == 1);
    se_vdp2_tilemap off = {};
    CHECK(se_get_vdp2_tilemap(ctx, SE_LAYER_NBG3, &off) == SE_OK);
    CHECK(off.active == 0);
    CHECK(off.map_width == 0);
    CHECK(off.map_height == 0);
    CHECK(off.tile_count == 0);
    CHECK(off.truncated == 0);
    CHECK(se_get_vdp2_tile_indices(ctx, SE_LAYER_NBG3, nullptr, 0) == 0);
    se_destroy(ctx);
}

void TestTileGrid()
{
    State state = MakeTiledNbg3State();
    se_context* ctx = Open(state);

    uint32_t w = 0;
    uint32_t h = 0;
    const std::vector<uint8_t> plain = RenderLayer(ctx, kLayerNbg3, false, w, h);
    std::vector<uint8_t> grid = RenderLayer(ctx, kLayerNbg3, true, w, h);
    CHECK(w == 32);
    CHECK(h == 8);
    // 8x8 characters with no scroll: the left edge of every character column, plus the
    // top row of the character row the frame starts on. Everything else is untouched.
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 32; ++x)
            CHECK(SamePixel(plain, grid, w, x, y) == !(x % 8 == 0 || y % 8 == 0));
    // The line is the grid colour blended half-and-half with the texel under it, which
    // over the first tile's white is (255, 235, 159).
    CHECK(IsRgba(grid, w, 0, 0, 255, 235, 159, 255));

    // Scrolling by half a cell moves the boundaries with the background rather than
    // leaving a fixed lattice drawn over it.
    SetReg(state, 0x094, 4);        // SCXN3: 4 dots right
    SetReg(state, 0x096, 4);        // SCYN3: 4 dots down
    CHECK(se_begin_frame(ctx) == SE_OK);
    const std::vector<uint8_t> scrolledPlain = RenderLayer(ctx, kLayerNbg3, false, w, h);
    grid = RenderLayer(ctx, kLayerNbg3, true, w, h);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 32; ++x)
            CHECK(SamePixel(scrolledPlain, grid, w, x, y) ==
                  !((x + 4) % 8 == 0 || (y + 4) % 8 == 0));

    se_destroy(ctx);
}

// A bitmap screen is one linear image: there is no tile map to export and no pattern grid
// to outline, and the export says so rather than writing an empty tileset.
void TestBitmapLayerHasNoTileMap()
{
    State state = MakeTiledNbg3State();
    SetReg(state, 0x020, 0x0009);   // BGON: NBG0 on
    SetReg(state, 0x028, 0x0002);   // CHCTLA: N0BMEN -- NBG0 in bitmap mode
    SetReg(state, 0x0F8, 0x0001);   // PRINA: NBG0 priority 1
    se_context* ctx = Open(state);
    const se_render_opts opts = AllLayers();

    se_vdp2_tilemap info = {};
    CHECK(se_get_vdp2_tilemap(ctx, SE_LAYER_NBG0, &info) == SE_OK);
    CHECK(info.active == 1);
    CHECK(info.bitmap == 1);
    CHECK(info.tile_count == 0);
    CHECK(info.map_width == 0);

    // A bitmap screen has no patterns, so asking for the grid changes nothing.
    uint32_t w = 0;
    uint32_t h = 0;
    const std::vector<uint8_t> plain = RenderLayer(ctx, kLayerNbg0, false, w, h);
    const std::vector<uint8_t> grid = RenderLayer(ctx, kLayerNbg0, true, w, h);
    CHECK(plain == grid);

    const LayerExport ex = BuildLayerExport(ctx, kLayerNbg0, opts, 3);
    CHECK(ex.files.size() == 1);
    CHECK(FindFile(ex, "nbg0_frame00003_layer.bmp") != nullptr);
    CHECK(!ex.note.empty());

    se_destroy(ctx);
}

void TestTileCsv()
{
    const uint32_t indices[6] = { 0, 1, 2, 30, 4, 500 };
    CHECK(BuildTileCsv(indices, 3, 2) == "0,1,2\n30,4,500\n");
    CHECK(BuildTileCsv(indices, 6, 1) == "0,1,2,30,4,500\n");
    CHECK(BuildTileCsv(indices, 0, 2).empty());
    CHECK(BuildTileCsv(nullptr, 3, 2).empty());
}

void TestVdp2Export()
{
    State state = MakeTiledNbg3State();
    se_context* ctx = Open(state);
    const se_render_opts opts = AllLayers();

    const LayerExport ex = BuildLayerExport(ctx, kLayerNbg3, opts, 7);
    CHECK(ex.folder == "nbg3_frame00007");
    CHECK(ex.files.size() == 3);
    CHECK(ex.note.empty());

    const ExportFile* layer = FindFile(ex, "nbg3_frame00007_layer.bmp");
    const ExportFile* tileset = FindFile(ex, "nbg3_frame00007_tileset.bmp");
    const ExportFile* csv = FindFile(ex, "nbg3_frame00007_tiles.csv");
    CHECK(layer != nullptr);
    CHECK(tileset != nullptr);
    CHECK(csv != nullptr);
    if (!layer || !tileset || !csv)
    {
        se_destroy(ctx);
        return;
    }

    uint32_t w = 0;
    uint32_t h = 0;
    uint16_t bpp = 0;
    BmpHeader(layer->bytes, w, h, bpp);
    CHECK(w == 32);
    CHECK(h == 8);
    CHECK(bpp == 32);   // the layer keeps its transparency

    BmpHeader(tileset->bytes, w, h, bpp);
    CHECK(w == SE_VDP2_TILESET_COLUMNS * 8);
    CHECK(h == 8);
    CHECK(bpp == 32);

    // The CSV is the tile map itself: 128 rows of 128 comma-separated indices.
    const std::string text(csv->bytes.begin(), csv->bytes.end());
    CHECK(std::count(text.begin(), text.end(), '\n') == 128);
    CHECK(text.compare(0, 16, "0,1,2,3,0,1,2,3,") == 0);
    const size_t firstLine = text.find('\n');
    CHECK(firstLine != std::string::npos);
    if (firstLine != std::string::npos)
        CHECK(std::count(text.begin(), text.begin() + firstLine, ',') == 127);

    // A layer BGON does not enable produces nothing, and says why.
    const LayerExport off = BuildLayerExport(ctx, kLayerNbg0, opts, 7);
    CHECK(off.files.empty());
    CHECK(!off.note.empty());

    se_destroy(ctx);
}

void TestVdp1Export()
{
    State state = MakeSpriteState();
    se_context* ctx = Open(state);
    const se_render_opts opts = AllLayers();

    CHECK(se_sprite_count(ctx) == 1);
    const LayerExport ex = BuildLayerExport(ctx, kLayerVdp1, opts, 12);
    CHECK(ex.folder == "vdp1_frame00012");
    // One image per sprite, plus the composed sprite layer.
    CHECK(ex.files.size() == se_sprite_count(ctx) + 1);
    CHECK(FindFile(ex, "vdp1_frame00012_layer.bmp") != nullptr);

    // Object number 0, command 1 (the system clip is command 0), texture at 0x100, 8x2.
    const ExportFile* sprite =
        FindFile(ex, "vdp1_frame00012_sprite000_cmd001_tex000100_8x2.bmp");
    CHECK(sprite != nullptr);
    if (sprite)
    {
        uint32_t w = 0;
        uint32_t h = 0;
        uint16_t bpp = 0;
        BmpHeader(sprite->bytes, w, h, bpp);
        CHECK(w == 8);
        CHECK(h == 2);
    }

    se_destroy(ctx);
}

// The encoders the export hands its pixels to (BinaryWriter.h), which is also what the
// Texture Viewer and the F12 screenshot write with.
void TestBmpEncoders()
{
    // 32-bpp BGRA: alpha survives, and rows are stored bottom-up.
    std::vector<uint8_t> rgba(2 * 2 * 4, 0);
    rgba[0] = 10; rgba[1] = 20; rgba[2] = 30; rgba[3] = 40;             // top-left
    rgba[(2 * 2 - 1) * 4 + 3] = 255;                                     // bottom-right
    const std::vector<uint8_t> bmp32 = BuildBmpRgba(2, 2, rgba);
    uint32_t w = 0;
    uint32_t h = 0;
    uint16_t bpp = 0;
    BmpHeader(bmp32, w, h, bpp);
    CHECK(w == 2);
    CHECK(h == 2);
    CHECK(bpp == 32);
    CHECK(bmp32.size() == 54 + 2 * 2 * 4);
    const size_t bottomRow = 54;                 // first stored row is the image's last
    CHECK(bmp32[bottomRow + 4 + 3] == 255);      // bottom-right pixel's alpha
    const size_t topRow = 54 + 2 * 4;
    CHECK(bmp32[topRow + 0] == 30);              // B
    CHECK(bmp32[topRow + 1] == 20);              // G
    CHECK(bmp32[topRow + 2] == 10);              // R
    CHECK(bmp32[topRow + 3] == 40);              // A

    // 8-bpp indexed: header, colour table, and one padded row per line.
    const uint8_t palette[8] = { 1, 2, 3, 0, 4, 5, 6, 0 };
    const std::vector<uint8_t> idx(3 * 2, 1);
    const std::vector<uint8_t> bmp8 = BuildBmp(3, 2, idx, 2, palette);
    BmpHeader(bmp8, w, h, bpp);
    CHECK(w == 3);
    CHECK(h == 2);
    CHECK(bpp == 8);
    CHECK(bmp8.size() == 54 + 8 + 2 * 4);        // 3-byte rows padded to 4
    CHECK(bmp8[54] == 1 && bmp8[55] == 2 && bmp8[56] == 3);
}

void TestWriteExport()
{
    State state = MakeTiledNbg3State();
    se_context* ctx = Open(state);
    const LayerExport ex = BuildLayerExport(ctx, kLayerNbg3, AllLayers(), 7);

    std::string dir;
    std::string error;
    CHECK(WriteLayerExport("se_layer_export_test", ex, dir, error));
    CHECK(error.empty());
    CHECK(!dir.empty());
    if (!dir.empty())
    {
        for (size_t i = 0; i < ex.files.size(); ++i)
        {
            const std::string path = dir + "/" + ex.files[i].name;
            std::ifstream f(path.c_str(), std::ios::binary);
            const std::vector<uint8_t> got((std::istreambuf_iterator<char>(f)),
                                           std::istreambuf_iterator<char>());
            CHECK(got == ex.files[i].bytes);
            std::remove(path.c_str());
        }
    }

    // An export with nothing in it reports the reason instead of writing an empty folder.
    LayerExport empty;
    empty.folder = "nothing";
    empty.note = "nothing to do";
    CHECK(!WriteLayerExport("se_layer_export_test", empty, dir, error));
    CHECK(error == "nothing to do");
    CHECK(dir.empty());

    se_destroy(ctx);
}

}  // namespace

int main()
{
    TestIsolatedLayerRender();
    TestTileMapAndTileset();
    TestTileMapShapeMatchesWithoutCounting();
    TestRebuildDoesNotLeakPreviousMap();
    TestTileGrid();
    TestBitmapLayerHasNoTileMap();
    TestTileCsv();
    TestVdp2Export();
    TestVdp1Export();
    TestBmpEncoders();
    TestWriteExport();

    if (gFailures)
    {
        std::cerr << gFailures << " check(s) failed\n";
        return 1;
    }
    std::cout << "LayerExportTests: all checks passed\n";
    return 0;
}
