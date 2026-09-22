// LayerExport — what the per-layer viewer panels write to disk.
//
// Deliberately free of ImGui and of IPlatform: an export is "read the core, produce named
// byte blobs, write them into a folder", which is testable against a synthetic snapshot
// with no renderer and no window (see FrontEnd/tests/LayerExportTests.cpp). The panels in
// LayerPanels.cpp only decide *when* to call this.
//
// Formats follow what the app already saves: BMP for images (see BinaryWriter.h — the
// Texture Viewer and screenshots write BMP too, so no new decoder is needed to open an
// export) and plain CSV for the tile map.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "saturnexplorer/SaturnExplorer.h"

namespace sfe
{

// Which layer a panel shows. A frontend concept: the core addresses VDP2 scroll screens by
// se_vdp2_layer and the sprite layer through se_render_opts::show_vdp1_sprites, and only
// the panels need one id spanning both. The VDP2 values match se_vdp2_layer.
enum LayerId
{
    kLayerNbg0  = SE_LAYER_NBG0,
    kLayerNbg1  = SE_LAYER_NBG1,
    kLayerNbg2  = SE_LAYER_NBG2,
    kLayerNbg3  = SE_LAYER_NBG3,
    kLayerRbg0  = SE_LAYER_RBG0,
    kLayerVdp1  = SE_LAYER_COUNT,   // the VDP1 sprite layer
    kLayerCount = SE_LAYER_COUNT + 1
};

// True for the five VDP2 scroll screens, false for the sprite layer — so 'layer' can be
// used as an se_vdp2_layer.
inline bool IsVdp2Layer(LayerId layer) { return layer != kLayerVdp1; }

// Render options that show 'layer' and nothing else, over transparency. This is the whole
// of "render one layer": se_render_frame with a different set of toggles.
se_render_opts LayerRenderOpts(const se_render_opts& base, LayerId layer, bool tileGrid);

// One file an export produces: its name inside the export folder, and its bytes.
struct ExportFile
{
    std::string          name;
    std::vector<uint8_t> bytes;
};

// A built export, before anything touches the filesystem: the folder it belongs in, its
// files, and a note about anything that could not be produced ("NBG2 is in bitmap mode —
// no tile map"). 'files' is empty when there was nothing to export at all.
struct LayerExport
{
    std::string             folder;
    std::vector<ExportFile> files;
    std::string             note;
};

// Run a core image call through its two-call size convention into 'rgba'. 'render' is
// invoked as render(se_image*, size_t*) — once to learn the size, once to fill. Returns
// false when the core declines.
template <typename Render>
bool FetchCoreImage(Render&& render, std::vector<uint8_t>& rgba, uint32_t& w, uint32_t& h)
{
    se_image img = {};
    size_t needed = 0;
    if (render(&img, &needed) != SE_OK || needed == 0)
    {
        return false;
    }
    rgba.resize(needed);
    img.pixels = rgba.data();
    img.capacity = rgba.size();
    if (render(&img, &needed) != SE_OK)
    {
        return false;
    }
    w = img.width;
    h = img.height;
    return true;
}

// Tile indices as CSV: one line per map row, values comma-separated, each line '\n'
// terminated. Each value indexes the tileset image laid out SE_VDP2_TILESET_COLUMNS wide,
// so index i is the tile at (i % COLUMNS, i / COLUMNS).
std::string BuildTileCsv(const uint32_t* indices, uint32_t width, uint32_t height);

// Short lowercase layer name used in folder and file names ("nbg0" ... "vdp1").
const char* LayerShortName(LayerId layer);

// Build the export for one layer from the context's current frame: for a VDP2 scroll
// screen the tileset image, the tile-index CSV and the composed layer image; for VDP1 one
// image per sprite plus the composed sprite layer. 'opts' supplies the shared render
// toggles (windows, colour calculation) exactly as the panel displays them, so what is
// written matches what was on screen — except the tile grid, which is a view aid and is
// left out of the exported pixels.
LayerExport BuildLayerExport(se_context* ctx, LayerId layer, const se_render_opts& opts,
                             uint64_t frame);

// Write a built export into '<root>/<folder>', creating the directories. On success sets
// 'outDir' to the folder written and returns true; otherwise fills 'error'. Needs a host
// filesystem — the panels gate this on IPlatform::HasHostFilesystem().
bool WriteLayerExport(const std::string& root, const LayerExport& ex, std::string& outDir,
                      std::string& error);

// Where exports go unless the user picks somewhere else: an "exports" folder beside the
// app's own settings, created on demand. Empty if the config directory is unresolvable.
std::string DefaultExportRoot();

}  // namespace sfe
