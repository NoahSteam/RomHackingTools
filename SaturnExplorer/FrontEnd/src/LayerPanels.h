// LayerPanels — one dockable viewer tab per graphics layer, beside VDP Output and 3D View.
//
// VDP Output shows the finished frame; these show what each layer contributes to it on its
// own — the VDP1 sprite layer and the five VDP2 scroll screens (NBG0-3, RBG0) — over a
// transparency checkerboard, so it is obvious which pixels a layer actually covers. A
// panel is just se_render_frame with one layer enabled and transparent_background set;
// the VDP2 tabs add the compositor's tile-grid overlay, and every tab exports what it is
// showing (see LayerExport.h, which owns the file formats and holds no ImGui).
//
// Kept out of App.cpp: this is a self-contained panel group whose only tie to App is the
// context + render options it is handed each frame.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "LayerExport.h"        // LayerId + the export artifacts
#include "Platform/IPlatform.h"

namespace sfe
{

class Settings;

// One layer tab. 'title' is the ImGui window title, so App's dock layout must use the same
// string; 'key' identifies the panel in settings and the Windows menu.
struct LayerPanelDesc
{
    LayerId     id;
    const char* key;
    const char* title;
};

// Every layer tab, in display order (sprites first, then the scroll screens).
const std::vector<LayerPanelDesc>& LayerPanelList();

// What the panels need from the app for one frame.
struct LayerPanelFrame
{
    se_context*           context = nullptr;
    const se_render_opts* opts = nullptr;   // the shared Layers toggles
    uint64_t              frame = 0;        // frame number, used in exported file names
};

class LayerPanels
{
public:
    void Load(const Settings& settings);
    void Save(Settings& settings) const;

    // Draw every panel whose visibility flag is set. 'visible' is indexed by LayerId and
    // must have kLayerCount entries.
    void Draw(const LayerPanelFrame& frame, const bool* visible, IPlatform& platform);

    // True once after the user changed a persisted preference (export folder, grid toggle).
    bool ConsumeSettingsDirty();

private:
    // Per-layer view state: the uploaded texture and the pixels behind it.
    struct View
    {
        TextureHandle        texture = 0;
        int                  width = 0;
        int                  height = 0;
        std::vector<uint8_t> pixels;     // RGBA, as uploaded
        bool                 showGrid = false;
        // Last export result for THIS layer. Per-view, not shared: six panels draw from one
        // LayerPanels, so a single status line would report NBG0's export under NBG1's
        // toolbar as well.
        std::string          status;
        bool                 statusError = false;
    };

    // The folder exports go to: the user's choice if set, else the default — resolved once
    // and cached, because resolving it creates the folder.
    const std::string& ExportRoot();

    void DrawPanel(const LayerPanelDesc& desc, const LayerPanelFrame& frame,
                   IPlatform& platform);
    void DrawToolbar(const LayerPanelDesc& desc, const LayerPanelFrame& frame,
                     IPlatform& platform);
    void RunExport(const LayerPanelDesc& desc, const LayerPanelFrame& frame,
                   IPlatform& platform);

    View        mViews[kLayerCount];
    std::string mExportRoot;          // the user's chosen folder; empty = use the default
    std::string mDefaultRoot;         // cached DefaultExportRoot()
    bool        mDefaultRootResolved = false;
    bool        mSettingsDirty = false;
};

}  // namespace sfe
