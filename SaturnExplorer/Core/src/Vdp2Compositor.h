// Vdp2Compositor — software-composites the VDP2 NBG scroll screens (the tiled
// backgrounds behind/around the VDP1 sprites) into an RGBA layer, the way the
// hardware builds each screen from its plane -> page -> pattern-name -> cell
// hierarchy. Ported from the validated Python prototype and cross-checked
// against Yabause's vidsoft.c. See ARCHITECTURE.md §7.
//
// Rather than blending straight into an RGBA buffer, every VDP2 source emits a per-pixel
// descriptor into a PixColumn (see PixelMixer.h): the back screen at priority 0, then each
// enabled NBG/RBG0 layer at its own priority. The VDP1 sprites emit into the same columns
// (Vdp2Compositor::EmitSprites), and Context resolves each column to one RGBA pixel. This
// four-deep column is what makes per-pixel sprite priority, cross-layer colour calculation,
// the line-colour screen and extended colour calculation natural rather than special cases --
// standard colour calculation only ever blends the top pixel with the one below it, and the
// extended form reads up to four.
//
// Modeled today: NBG0-3 (cell + bitmap), RBG0 rotation, fractional/line scroll + zoom,
// windows (including the sprite and colour-calculation windows), per-screen colour calculation
// (with second-image ratio, line colour insertion and the extended form), colour offset applied
// after colour calculation, horizontal mosaic, the real back screen, the sprite layer's own
// colour calculation, sprite shadows, gradation calculation, vertical cell scroll, and RBG1
// (NBG0's slot, rotation set B), the special priority and colour-calculation functions, and the
// exclusive monitor modes. Vertical mosaic is not modeled yet.
#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "saturnexplorer/SeTypes.h"
#include "HardwareSnapshot.h"
#include "PixelMixer.h"
#include "SpriteLayer.h"

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

// Side inputs and outputs of the layer emitters, which all share one frame's pixel grid.
struct EmitExtras
{
    // In: the finished sprite layer, whose window bits feed every layer's window logic.
    const std::vector<SpritePixel>* sprites = nullptr;
    // Out: the colour of the screen CCCTL's gradation calculation is set up for, per pixel.
    std::vector<Rgba>* gradation = nullptr;
    // Out: RBG0's coefficient-table line colour bits (0xFF where the table supplies none).
    std::vector<uint8_t>* lineOverride = nullptr;
};

class Vdp2Compositor
{
public:
    // Upper bound on distinct tiles kept per screen. RBG0's 4x4 plane grid can address a
    // 512x512-pattern map, so a pathological (or garbage) map could otherwise name far
    // more tiles than any real background has. Comfortably above a whole 512 KiB of 8x8
    // 4bpp characters (16384), so a real screen is never truncated.
    static const uint32_t kMaxTiles = 32768;

    // A screen the compositor draws, as the 3D view needs to order it: which layer toggle shows it
    // (RBG1 answers to NBG0's), its base priority, and the hardware's rank among equal priorities
    // (kRank* in PixelMixer.h; a higher rank is in front). 'liftsToOne' marks a screen whose base
    // priority is 0 and which is drawn only because the special priority function can raise some of
    // its pixels to 1. The set is exactly the one EmitLayers draws.
    struct DisplayedScreen
    {
        int     toggle = 0;
        uint8_t priority = 0;
        uint8_t rank = 0;
        bool    liftsToOne = false;
    };
    static void DisplayedScreens(const HardwareSnapshot& snapshot, const se_render_opts& opts,
                                 std::vector<DisplayedScreen>& out);

    // Emit every enabled NBG/RBG0 layer into 'cols' (one PixColumn per pixel, sized
    // width*height) at its VDP2 priority. Layers are emitted back-to-front (ascending
    // priority, higher-numbered NBG first on ties) so a same-priority sprite emitted
    // afterwards wins the tie, exactly as VDP1 sprites sit in front of same-priority NBGs
    // on hardware. Honors opts.show_layer[] and the BGON enable bits; priority-0 layers
    // (not displayed) are skipped. A no-op when the snapshot lacks VDP2 VRAM or registers.
    //
    // 'extras' carries the sprite layer in (its window bits feed each layer's window logic) and the
    // gradation / line-colour side buffers out.
    static void EmitLayers(const HardwareSnapshot& snapshot, const se_render_opts& opts,
                           int width, int height, std::vector<PixColumn>& cols,
                           const EmitExtras& extras = EmitExtras());

    // Emit the sprite layer into 'cols' after every VDP2 layer, so a sprite wins a priority tie.
    // Applies the sprite layer's own window, colour calculation (the pixel's ratio, enabled by its
    // SPCCCS condition) and, when opts.show_shadow_highlight is set, sprite shadows: a shadow marker
    // darkens the layer under it, and a self-shadowed pixel darkens itself.
    static void EmitSprites(const HardwareSnapshot& snapshot, const se_render_opts& opts,
                            int width, int height, const std::vector<SpritePixel>& sprites,
                            const SpritePriorityTable& prios, std::vector<PixColumn>& cols,
                            const EmitExtras& extras = EmitExtras());

    // Everything the mixer reads from the VDP2 registers when it resolves a column: the colour-
    // calculation modes (second-image ratio, extended), the line colour screen's per-line colours
    // and ratio, and the two colour offsets. Call after the layers are emitted, before
    // ResolveColumns.
    static MixState ReadMixState(const HardwareSnapshot& snapshot, const se_render_opts& opts,
                                 int width, int height, const EmitExtras& extras = EmitExtras());

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
    // fresh map would rehash its way back up from nothing each time.
    //
    // Do NOT reserve() it. A reserve makes libc++'s bucket count a power of two, which
    // switches bucket selection from a modulus to a mask -- and the mask keeps the low
    // bits, where a tile key has no entropy. Measured at 65,536 lookups: 0.12 ms letting
    // clear() keep the previous run's (prime) buckets, against 18 ms reserving for 512
    // distinct tiles and 2,000 ms for 32,768. That is a hash collapse, not allocation
    // cost. BuildTileMap now mixes the key so the structure no longer depends on the
    // growth policy, but reserving still buys nothing here.
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
