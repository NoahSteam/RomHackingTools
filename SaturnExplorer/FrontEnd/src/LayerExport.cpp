#include "LayerExport.h"

#include <cstdarg>
#include <cstdio>

#include "BinaryWriter.h"
#include "FileWrite.h"  // the checked write/close and the directory primitives
#include "Settings.h"   // the config dir the default export root lives under

namespace sfe
{

namespace
{

// The game's own palette for a sprite texture, when its color mode has one. LUT sprites
// read a VDP1 CLUT; bank sprites index a CRAM sub-palette; RGB555 carries its colour in
// the texel and has no palette at all.
bool SpritePalette(se_context* ctx, const se_texture_ref& ref, se_palette& pal)
{
    if (ref.color_mode == SE_COLOR_RGB555)
    {
        return false;
    }
    const se_result r = (ref.color_mode == SE_COLOR_LUT_16)
                            ? se_decode_palette(ctx, ref.clut_address, &pal)
                            : se_decode_bank_palette(ctx, ref.palette_bank, ref.color_mode, &pal);
    return r == SE_OK && pal.count > 0;
}

std::string Format(const char* fmt, ...)
{
    char buf[192];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    return std::string(buf);
}

// The composed layer image, exactly as the panel shows it minus the tile grid — that is a
// view aid, not something to bake into an exported picture. Every export leads with this,
// whichever layer it is.
void AddComposedLayer(se_context* ctx, LayerId layer, const se_render_opts& opts,
                      const std::string& stem, LayerExport& ex)
{
    std::vector<uint8_t> rgba;
    uint32_t w = 0;
    uint32_t h = 0;
    const se_render_opts one = LayerRenderOpts(opts, layer, false);
    if (!FetchCoreImage([&](se_image* i, size_t* n) { return se_render_frame(ctx, &one, i, n); },
                        rgba, w, h))
    {
        return;
    }
    ExportFile f;
    f.name = stem + "_layer.bmp";
    f.bytes = BuildBmpRgba(static_cast<int>(w), static_cast<int>(h), rgba);
    ex.files.push_back(f);
}

// A VDP2 scroll screen's tile data: the tileset it draws from, and the map that arranges it.
void AddVdp2Tiles(se_context* ctx, se_vdp2_layer layer, const se_vdp2_tilemap& info,
                  const std::string& stem, LayerExport& ex)
{
    std::vector<uint8_t> rgba;
    uint32_t w = 0;
    uint32_t h = 0;
    if (FetchCoreImage([&](se_image* i, size_t* n)
                       { return se_render_vdp2_tileset(ctx, layer, SE_VDP2_TILESET_COLUMNS, i, n); },
                       rgba, w, h))
    {
        ExportFile f;
        f.name = stem + "_tileset.bmp";
        f.bytes = BuildBmpRgba(static_cast<int>(w), static_cast<int>(h), rgba);
        ex.files.push_back(f);
    }

    const size_t cells = static_cast<size_t>(info.map_width) * info.map_height;
    if (cells == 0)
    {
        return;
    }
    std::vector<uint32_t> indices(cells, 0);
    if (se_get_vdp2_tile_indices(ctx, layer, indices.data(), indices.size()) != cells)
    {
        return;
    }
    const std::string csv = BuildTileCsv(indices.data(), info.map_width, info.map_height);
    ExportFile f;
    f.name = stem + "_tiles.csv";
    f.bytes.assign(csv.begin(), csv.end());
    ex.files.push_back(f);
}

// Every VDP1 sprite as its own image.
void AddVdp1Sprites(se_context* ctx, const std::string& stem, LayerExport& ex)
{
    const size_t count = se_sprite_count(ctx);
    if (count == 0)
    {
        ex.note = "No VDP1 sprites are on screen in this frame.";
        return;
    }

    std::vector<uint8_t> rgba;
    size_t failed = 0;
    for (size_t i = 0; i < count; ++i)
    {
        se_sprite_2d sprite = {};
        if (se_get_sprite_2d(ctx, i, &sprite) != SE_OK)
        {
            continue;
        }
        const se_texture_ref& ref = sprite.texture;
        uint32_t w = 0;
        uint32_t h = 0;
        if (ref.width == 0 || ref.height == 0 ||
            !FetchCoreImage([&](se_image* im, size_t* n) { return se_decode_texture(ctx, &ref, im, n); },
                            rgba, w, h))
        {
            ++failed;
            continue;
        }
        const int iw = static_cast<int>(w);
        const int ih = static_cast<int>(h);
        se_palette pal = {};
        ExportFile f;
        f.name = Format("%s_sprite%03u_cmd%03u_tex%06X_%ux%u.bmp", stem.c_str(),
                        sprite.object_number, sprite.command_index, ref.vram_address,
                        static_cast<unsigned>(w), static_cast<unsigned>(h));
        f.bytes = SpritePalette(ctx, ref, pal) ? BuildIndexedBmp(iw, ih, rgba, pal)
                                               : BuildBmpRgba(iw, ih, rgba);
        ex.files.push_back(f);
    }

    if (failed > 0)
    {
        ex.note = Format("%zu of %zu sprites had no decodable texture and were skipped.",
                         failed, count);
    }
}

}  // namespace

se_render_opts LayerRenderOpts(const se_render_opts& base, LayerId layer, bool tileGrid)
{
    se_render_opts one = base;
    for (int i = 0; i < SE_LAYER_COUNT; ++i)
    {
        one.show_layer[i] = (i == static_cast<int>(layer)) ? 1 : 0;
    }
    one.show_vdp1_sprites = (layer == kLayerVdp1) ? 1 : 0;
    one.transparent_background = 1;
    one.show_tile_grid = tileGrid ? 1 : 0;
    return one;
}

std::string BuildTileCsv(const uint32_t* indices, uint32_t width, uint32_t height)
{
    std::string csv;
    if (!indices || width == 0 || height == 0)
    {
        return csv;
    }
    csv.reserve(static_cast<size_t>(width) * height * 4);
    char num[16];
    for (uint32_t y = 0; y < height; ++y)
    {
        for (uint32_t x = 0; x < width; ++x)
        {
            if (x) csv.push_back(',');
            std::snprintf(num, sizeof(num), "%u", indices[static_cast<size_t>(y) * width + x]);
            csv += num;
        }
        csv.push_back('\n');
    }
    return csv;
}

const char* LayerShortName(LayerId layer)
{
    switch (layer)
    {
    case kLayerNbg0: return "nbg0";
    case kLayerNbg1: return "nbg1";
    case kLayerNbg2: return "nbg2";
    case kLayerNbg3: return "nbg3";
    case kLayerRbg0: return "rbg0";
    case kLayerVdp1: return "vdp1";
    default:         return "layer";
    }
}

LayerExport BuildLayerExport(se_context* ctx, LayerId layer, const se_render_opts& opts,
                             uint64_t frame)
{
    LayerExport ex;
    if (!ctx)
    {
        ex.note = "No data is loaded.";
        return ex;
    }

    // A VDP2 screen that is not being displayed has nothing to show and nothing to write;
    // say so rather than exporting a folder of blank images.
    se_vdp2_tilemap info = {};
    if (IsVdp2Layer(layer))
    {
        se_get_vdp2_tilemap(ctx, static_cast<se_vdp2_layer>(layer), &info);
        if (!info.active)
        {
            ex.note = "This layer is not enabled in the current frame — nothing to export.";
            return ex;
        }
    }

    // Both the folder and every file inside it carry the layer and the frame, so a file
    // stays self-describing after it is dragged out of the folder it was written in.
    const std::string stem =
        Format("%s_frame%05llu", LayerShortName(layer), static_cast<unsigned long long>(frame));
    ex.folder = stem;
    AddComposedLayer(ctx, layer, opts, stem, ex);

    if (layer == kLayerVdp1)
    {
        AddVdp1Sprites(ctx, stem, ex);
    }
    else if (info.bitmap)
    {
        ex.note = "This layer is in bitmap mode — it has no tile map, so only the "
                  "composed layer image was written.";
    }
    else
    {
        AddVdp2Tiles(ctx, static_cast<se_vdp2_layer>(layer), info, stem, ex);
        if (info.truncated)
        {
            ex.note = "The tile map named more distinct tiles than the extractor keeps; "
                      "the excess was written as tile 0.";
        }
    }
    return ex;
}

bool ExportFs::WriteFile(const std::string& path, const void* data, size_t size,
                         std::string& error)
{
    return WriteFileAtomically(path, data, size, error);
}
bool ExportFs::MakeDir(const std::string& path)       { return MakeDirectory(path); }
bool ExportFs::RemoveFlatDir(const std::string& path) { return RemoveFlatDirectory(path); }
bool ExportFs::Move(const std::string& from, const std::string& to) { return MovePath(from, to); }
bool ExportFs::Exists(const std::string& path)        { return FileOrDirectoryExists(path); }

bool WriteLayerExport(const std::string& root, const LayerExport& ex, std::string& outDir,
                      std::string& error, ExportFs* fs)
{
    outDir.clear();
    error.clear();
    if (ex.files.empty())
    {
        error = ex.note.empty() ? "There is nothing to export." : ex.note;
        return false;
    }
    if (root.empty())
    {
        error = "No export folder is set.";
        return false;
    }
    ExportFs real;
    ExportFs& io = fs ? *fs : real;

    const char sep = PathSeparator();
    const std::string dir     = root + sep + ex.folder;
    // Siblings of the destination, so publishing is a rename within one filesystem.
    const std::string staging = dir + ".separt";
    const std::string backup  = dir + ".seold";

    if (!io.MakeDir(root))
    {
        error = "Could not create the export folder " + root;
        return false;
    }
    // A staging or backup directory still present is debris from a crash or an earlier
    // failure, not state worth keeping. Clearing both makes the two renames below
    // unambiguous.
    io.RemoveFlatDir(staging);
    io.RemoveFlatDir(backup);
    if (!io.MakeDir(staging))
    {
        error = "Could not create a temporary folder beside " + dir;
        return false;
    }

    for (size_t i = 0; i < ex.files.size(); ++i)
    {
        std::string why;
        if (!io.WriteFile(staging + sep + ex.files[i].name, ex.files[i].bytes.data(),
                          ex.files[i].bytes.size(), why))
        {
            // Nothing is published yet, so there is nothing to roll back: drop the staging
            // directory and any previous export at 'dir' is still exactly as it was.
            io.RemoveFlatDir(staging);
            error = why.empty() ? ("Could not write " + ex.files[i].name) : why;
            return false;
        }
    }

    // Publish. Two renames rather than one replace, because neither POSIX nor Windows will
    // rename onto an existing directory: the old export steps aside, the new one takes the
    // name, and only then is the old one deleted.
    const bool hadPrevious = io.Exists(dir);
    if (hadPrevious && !io.Move(dir, backup))
    {
        io.RemoveFlatDir(staging);
        error = "Could not replace the previous export at " + dir;
        return false;
    }
    if (!io.Move(staging, dir))
    {
        // Put the previous export back before reporting: losing it to a failed publish is
        // the exact outcome this function exists to prevent.
        if (hadPrevious) io.Move(backup, dir);
        io.RemoveFlatDir(staging);
        error = "Could not publish the export to " + dir;
        return false;
    }
    if (hadPrevious) io.RemoveFlatDir(backup);

    outDir = dir;
    return true;
}

std::string DefaultExportRoot()
{
    return Settings::ConfigSubDir("exports", true);
}

}  // namespace sfe
