#include "LayerPanels.h"

#include <cstdio>

#include "imgui.h"

#include "PanelWidgets.h"   // Checkerboard, FitToDisplayAspect, ImageCornerLabel
#include "Settings.h"

namespace sfe
{

namespace
{

// What Export will write, for the button's tooltip.
const char* ExportSummary(LayerId layer)
{
    return (layer == kLayerVdp1)
        ? "Write the composed sprite layer, plus every sprite on screen as its own BMP."
        : "Write three files: the composed layer image as shown, the layer's tileset as "
          "one BMP, and its tile indices as CSV.";
}

// The tile map behind a VDP2 panel, for the header line and to know whether a tile grid is
// even meaningful. Zeroed for the sprite layer, which has no scroll map.
se_vdp2_tilemap TileMapOf(const LayerPanelFrame& frame, LayerId layer)
{
    se_vdp2_tilemap info = {};
    if (frame.hasData && frame.context && IsVdp2Layer(layer))
    {
        se_get_vdp2_tilemap(frame.context, static_cast<se_vdp2_layer>(layer), &info);
    }
    return info;
}

}  // namespace

const std::vector<LayerPanelDesc>& LayerPanelList()
{
    static const std::vector<LayerPanelDesc> kList = {
        { kLayerVdp1, "layerVdp1", "VDP1 Sprites" },
        { kLayerNbg0, "layerNbg0", "NBG0" },
        { kLayerNbg1, "layerNbg1", "NBG1" },
        { kLayerNbg2, "layerNbg2", "NBG2" },
        { kLayerNbg3, "layerNbg3", "NBG3" },
        { kLayerRbg0, "layerRbg0", "RBG0" },
    };
    return kList;
}

void LayerPanels::Load(const Settings& settings)
{
    mExportRoot = settings.Get("export", "dir", std::string());
    for (const LayerPanelDesc& desc : LayerPanelList())
    {
        if (!IsVdp2Layer(desc.id)) continue;   // the sprite layer has no tile grid
        mViews[desc.id].showGrid = settings.GetBool("layergrid", desc.key, false);
    }
}

void LayerPanels::Save(Settings& settings) const
{
    settings.Set("export", "dir", mExportRoot);
    for (const LayerPanelDesc& desc : LayerPanelList())
    {
        if (!IsVdp2Layer(desc.id)) continue;
        settings.SetBool("layergrid", desc.key, mViews[desc.id].showGrid);
    }
}

const std::string& LayerPanels::ExportRoot()
{
    if (!mExportRoot.empty()) return mExportRoot;
    if (!mDefaultRootResolved)
    {
        // Resolving creates the folder, so do it once whatever the answer — an
        // unresolvable config dir must not retry a chain of mkdirs every frame.
        mDefaultRootResolved = true;
        mDefaultRoot = DefaultExportRoot();
    }
    return mDefaultRoot;
}

bool LayerPanels::ConsumeSettingsDirty()
{
    const bool dirty = mSettingsDirty;
    mSettingsDirty = false;
    return dirty;
}

void LayerPanels::Draw(const LayerPanelFrame& frame, const bool* visible,
                       IPlatform& platform)
{
    for (const LayerPanelDesc& desc : LayerPanelList())
    {
        if (visible && !visible[desc.id]) continue;
        DrawPanel(desc, frame, platform);
    }
}

void LayerPanels::DrawToolbar(const LayerPanelDesc& desc, const LayerPanelFrame& frame,
                              IPlatform& platform)
{
    const bool canWrite = platform.HasHostFilesystem();
    ImGui::BeginDisabled(!frame.hasData || !canWrite);
    if (ImGui::Button("Export")) RunExport(desc, frame, platform);
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("%s", canWrite
        ? ExportSummary(desc.id)
        : "Exporting writes a folder of files, which this build has no filesystem for.");

    // A bitmap screen is one linear image with no character patterns, so there is no grid
    // to draw — ask the core rather than assuming which layers are tiled.
    const se_vdp2_tilemap info = TileMapOf(frame, desc.id);
    if (IsVdp2Layer(desc.id))
    {
        ImGui::SameLine();
        ImGui::BeginDisabled(frame.hasData && info.bitmap);
        if (ImGui::Checkbox("Tile Grid", &mViews[desc.id].showGrid)) mSettingsDirty = true;
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("%s", (frame.hasData && info.bitmap)
            ? "This layer is in bitmap mode — it has no character patterns to outline."
            : "Outline this screen's character-pattern boundaries. The grid is drawn from "
              "the same plane coordinates the pixels are sampled at, so it follows scroll, "
              "zoom and rotation instead of sitting on top as a fixed lattice.");
    }

    // The destination is shown, not hidden behind a dialog, so it is obvious where an
    // export landed — and clickable, so it can be changed without leaving the panel.
    if (canWrite)
    {
        const std::string& root = ExportRoot();
        ImGui::SameLine();
        ImGui::TextDisabled("to");
        ImGui::SameLine();
        if (ImGui::SmallButton(root.empty() ? "(choose a folder)" : root.c_str()))
        {
            std::string picked;
            if (platform.PickDirectory(picked) && !picked.empty())
            {
                mExportRoot = picked;
                mSettingsDirty = true;
            }
        }
        ImGui::SetItemTooltip("%s\nClick to choose a different export folder.",
                              root.empty() ? "No export folder could be resolved."
                                           : root.c_str());
        if (!root.empty())
        {
            ImGui::SameLine();
            if (ImGui::SmallButton("Reveal") && !platform.RevealPath(root.c_str()))
            {
                mStatus = "This build cannot open a file manager.";
                mStatusError = true;
            }
        }
    }

    if (info.active && !info.bitmap)
    {
        // color_count is 0 in the direct RGB modes, which index no palette at all.
        char colours[24] = "direct RGB";
        if (info.color_count)
            std::snprintf(colours, sizeof(colours), "%u colours", info.color_count);
        ImGui::TextDisabled("%ux%u map of %ux%u patterns  -  %u distinct tiles  -  %s",
                            info.map_width, info.map_height, info.cell_pixels,
                            info.cell_pixels, info.tile_count, colours);
    }
    if (!mStatus.empty())
    {
        const ImVec4 tint = mStatusError ? ImVec4(0.90f, 0.45f, 0.40f, 1.0f)
                                         : ImVec4(0.55f, 0.85f, 0.55f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, tint);
        ImGui::TextWrapped("%s", mStatus.c_str());
        ImGui::PopStyleColor();
    }
}

void LayerPanels::DrawPanel(const LayerPanelDesc& desc, const LayerPanelFrame& frame,
                            IPlatform& platform)
{
    // Begin returns false for a collapsed window or an unselected dock tab, so only the
    // layer actually on screen pays for a render.
    if (!ImGui::Begin(desc.title))
    {
        ImGui::End();
        return;
    }

    DrawToolbar(desc, frame, platform);
    ImGui::Separator();

    View& view = mViews[desc.id];
    uint32_t w = 0;
    uint32_t h = 0;
    const se_render_opts one = frame.opts
        ? LayerRenderOpts(*frame.opts, desc.id, view.showGrid)
        : se_render_opts();
    const bool rendered =
        frame.hasData && frame.context && frame.opts &&
        FetchCoreImage([&](se_image* i, size_t* n)
                       { return se_render_frame(frame.context, &one, i, n); },
                       view.pixels, w, h);
    if (!rendered)
    {
        ImGui::TextDisabled("No data loaded. File > Open Memory Dump...");
        ImGui::End();
        return;
    }

    const int iw = static_cast<int>(w);
    const int ih = static_cast<int>(h);
    view.texture = EnsureTexture(platform, view.texture, view.width, view.height, iw, ih);
    if (view.texture != 0) platform.UpdateTexture(view.texture, view.pixels.data(), iw, ih);

    ImVec2 pos;
    const ImVec2 size = FitToDisplayAspect(ImGui::GetContentRegionAvail(), pos);
    // A layer covers only part of the screen; the checkerboard is what makes the gaps read
    // as "this layer draws nothing here" rather than as black.
    Checkerboard(pos, size, 8.0f);
    ImGui::SetCursorScreenPos(pos);
    ImGui::Image(view.texture, size);

    char label[32];
    std::snprintf(label, sizeof(label), "%dx%d", iw, ih);
    ImageCornerLabel(pos, label);

    ImGui::End();
}

void LayerPanels::RunExport(const LayerPanelDesc& desc, const LayerPanelFrame& frame,
                            IPlatform& platform)
{
    if (!frame.hasData || !frame.context || !frame.opts)
    {
        mStatus = "No data is loaded.";
        mStatusError = true;
        return;
    }
    if (!platform.HasHostFilesystem())
    {
        mStatus = "This build has no filesystem to export to.";
        mStatusError = true;
        return;
    }
    const LayerExport built =
        BuildLayerExport(frame.context, desc.id, *frame.opts, frame.frame);
    std::string dir;
    std::string error;
    if (!WriteLayerExport(ExportRoot(), built, dir, error))
    {
        mStatus = error;
        mStatusError = true;
        return;
    }
    char msg[512];
    std::snprintf(msg, sizeof(msg), "Exported %zu file%s to %s", built.files.size(),
                  built.files.size() == 1 ? "" : "s", dir.c_str());
    mStatus = msg;
    if (!built.note.empty()) mStatus += "  (" + built.note + ")";
    mStatusError = false;
}

}  // namespace sfe
