// Vdp2Compositor — software-composites the VDP2 NBG scroll screens (the tiled
// backgrounds behind/around the VDP1 sprites) into an RGBA layer, the way the
// hardware builds each screen from its plane -> page -> pattern-name -> cell
// hierarchy. Ported from the validated Python prototype and cross-checked
// against Yabause's vidsoft.c. See ARCHITECTURE.md §7.
//
// Scope today: the four normal backgrounds (NBG0-3), cell (non-bitmap) mode,
// 1- and 2-word pattern names, and every cell color format (16/256/2048-color
// palette, RGB555, RGB888), normal and line-window clipping for NBG0-3, the real
// back-screen color (BKTA, single-colour or per-line), and per-screen color
// calculation (CCCTL/CCRN, ratio + additive blending against the layers below), the RBG0
// rotation screen (rotation parameter set A/B, coefficient tables, screen-over repeat),
// bitmap mode (NBG0/1 + RBG0), NBG0/1 fractional scroll, zoom, and per-line scroll/zoom,
// per-screen colour offset (CLOFEN/COxR/G/B), and horizontal mosaic. Vertical cell
// scroll, vertical mosaic, shadow, special priority/colour-calc, RBG1, and RPMD
// per-dot/window parameter selection are not modeled yet.
//
// Rather than blending straight into an RGBA buffer, every VDP2 source emits a per-pixel
// descriptor into a PixColumn (see PixelMixer.h): the back screen at priority 0, then each
// enabled NBG/RBG0 layer at its own priority. The VDP1 sprites emit into the same columns
// (Vdp1Rasterizer::EmitSprites), and Context resolves each column to one RGBA pixel. This
// two-deep column is what makes per-pixel sprite priority, cross-layer colour calculation,
// and the line-colour screen natural rather than special cases — colour calculation on
// VDP2 only ever blends the top-priority pixel with the one immediately below it.
//
// Modeled today: NBG0-3 (cell + bitmap), RBG0 rotation, fractional/line scroll + zoom,
// windows, per-screen colour calculation and colour offset, horizontal mosaic, and the
// real back screen. Sprite windows, the line-colour screen, vertical cell scroll, vertical
// mosaic, and RBG1 are not modeled yet.
#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "saturnexplorer/SeTypes.h"
#include "HardwareSnapshot.h"
#include "PixelMixer.h"

namespace se
{

// One distinct character pattern a scroll screen draws from: where its pixels live in
// VDP2 VRAM and the palette base its indexed texels resolve through. Two map cells that
// name the same character with different palettes are different tiles, because they are
// different pictures — flip is a property of the placement, not of the tile.
struct Vdp2Tile
{
    uint32_t charBase;   // byte address of the pattern's first cell in VDP2 VRAM
    uint32_t palette;    // palette base added to each indexed texel
};

// A scroll screen's whole plane -> page -> pattern grid, reduced to a tileset plus one
// index per map cell. This is the layer-panel export's model of the background: the
// tileset is the art, 'indices' is the map that arranges it.
// The half of a tile map the VDP2 registers decide by themselves. Split out so the
// distinction the builders draw -- registers describe the map, VRAM holds it -- is in the
// type rather than only in a comment, and so Reset() cannot drift as fields are added.
struct Vdp2TileMapShape
{
    bool     active = false;     // BGON enables the screen and its priority is non-zero
    bool     bitmap = false;     // bitmap mode: one linear image, no tiles
    bool     truncated = false;  // distinct tiles hit kMaxTiles; the excess maps to tile 0
    uint32_t cellPixels = 8;     // pattern edge in pixels (8 or 16)
    uint32_t colorCount = 0;     // palette entries a tile indexes; 0 in the RGB modes
    uint32_t mapWidth = 0;       // patterns across
    uint32_t mapHeight = 0;      // patterns down
};

struct Vdp2TileMap : Vdp2TileMapShape
{
    std::vector<uint32_t> indices;   // mapWidth * mapHeight, row-major, into 'tiles'
    std::vector<Vdp2Tile> tiles;

    // Clear the description but keep the containers' capacity: on a live source every
    // frame re-derives, so assigning a fresh Vdp2TileMap would free and re-allocate up to
    // a megabyte of indices each time. Assigning the base covers every scalar by
    // construction, so a field added above needs no edit here.
    void Reset()
    {
        static_cast<Vdp2TileMapShape&>(*this) = Vdp2TileMapShape();
        indices.clear();
        tiles.clear();
    }
};

class Vdp2Compositor
{
public:
    // Upper bound on distinct tiles kept per screen. RBG0's 4x4 plane grid can address a
    // 512x512-pattern map, so a pathological (or garbage) map could otherwise name far
    // more tiles than any real background has. Comfortably above a whole 512 KiB of 8x8
    // 4bpp characters (16384), so a real screen is never truncated.
    static const uint32_t kMaxTiles = 32768;

    // Emit every enabled NBG/RBG0 layer into 'cols' (one PixColumn per pixel, sized
    // width*height) at its VDP2 priority. Layers are emitted back-to-front (ascending
    // priority, higher-numbered NBG first on ties) so a same-priority sprite emitted
    // afterwards wins the tie, exactly as VDP1 sprites sit in front of same-priority NBGs
    // on hardware. Honors opts.show_layer[] and the BGON enable bits; priority-0 layers
    // (not displayed) are skipped. A no-op when the snapshot lacks VDP2 VRAM or registers.
    static void EmitLayers(const HardwareSnapshot& snapshot, const se_render_opts& opts,
                           int width, int height, std::vector<PixColumn>& cols);

    // Seed the VDP2 back screen (the always-present backdrop below every screen) into
    // every column at priority 0, reading its colour from the BKTA table in VDP2 VRAM —
    // a single colour, or one colour per display line when BKTA's per-line bit is set.
    // Call this before EmitLayers so translucent layers have a real surface to blend
    // against (colour calculation composites against whatever is below). A no-op when the
    // snapshot lacks VDP2 VRAM/registers; the caller keeps its own fallback backdrop then.
    static void SeedBackScreen(const HardwareSnapshot& snapshot, int width, int height,
                               std::vector<PixColumn>& cols);

    // Reduce one scroll screen's pattern-name tables to a tileset + index map (see
    // Vdp2TileMap). Reads the whole plane grid, not just what is on screen, so the
    // export describes the background rather than the current scroll window.
    // 'scratch' is the builder's charbase+palette -> index map. It is a parameter rather
    // than a local or a member of Vdp2TileMap because the reuse belongs to whoever owns
    // the cache slot, not to the description: a live source rebuilds every frame, and a
    // fresh map would rehash its way back up to kMaxTiles buckets each time.
    typedef std::unordered_map<uint64_t, uint32_t> TileScratch;
    static void BuildTileMap(const HardwareSnapshot& snapshot, int layer, Vdp2TileMap& out,
                             TileScratch& scratch);

    // Only what the VDP2 registers decide: active, bitmap, cellPixels, colorCount and the
    // map dimensions. Leaves 'indices' and 'tiles' empty, so it costs a register read
    // rather than a walk of up to 512x512 pattern names -- which is what a panel toolbar
    // wants when it is only sizing a header line every frame.
    static void BuildTileMapShape(const HardwareSnapshot& snapshot, int layer,
                                  Vdp2TileMap& out);

    // Pixel size of the image RenderTileset draws for 'map' at 'columns' tiles per row.
    // The single source of the layout, so the buffer the caller sizes and the pixels
    // written into it cannot disagree. Both are 0 when the screen has no tiles.
    static void TilesetSize(const Vdp2TileMap& map, uint32_t columns,
                            uint32_t& outWidth, uint32_t& outHeight);

    // Draw a screen's tileset as one RGBA image at TilesetSize's dimensions, in tile-index
    // order (so index i is at (i % columns, i / columns)). Transparent texels get alpha 0.
    // Returns false — leaving 'rgba' empty — when the screen has no tiles.
    static bool RenderTileset(const HardwareSnapshot& snapshot, int layer,
                              const Vdp2TileMap& map, uint32_t columns,
                              std::vector<uint8_t>& rgba);
};

}  // namespace se
