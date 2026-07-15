// App — the portable frontend. Owns the core context and its data source,
// holds UI state (render toggles, current selection), and draws every panel
// each frame. Depends only on the core (Seam B), ImGui, and IPlatform — no OS
// or GPU types — so it is identical across platforms.
#pragma once

#include <cstdint>
#include <vector>

#include "saturnexplorer/saturnexplorer.h"

#include "Platform/IPlatform.h"

namespace sfe
{

class App
{
public:
    void Initialize();
    void Shutdown();

    // Load Saturn state from a linear memory dump; replaces any current context.
    bool OpenFullDump(const char* path, uint32_t baseAddress);

    // Draw the whole UI. Called once per frame, between the platform's
    // BeginFrame and EndFrame.
    void BuildUI(IPlatform& platform);

private:
    void CloseData();
    void RenderFrameToTexture(IPlatform& platform);
    void DrawMenuBar(IPlatform& platform);
    void DrawLayerControls();
    void DrawVdpOutput(IPlatform& platform);
    void DrawCommandList();
    void DrawSelectedObject();
    void DrawVramMap();
    void DrawArchiveExplorer();
    void DrawTextureViewer();
    void DrawPaletteViewer();
    void DrawReferences();
    void DrawMemoryHistory();
    void DrawSearch();

    se_data_source mDataSource {};
    se_context*    mContext = nullptr;
    bool           mbHasData = false;

    se_render_opts mRenderOpts {};
    int            mSelectedCommand = -1;

    // VDP Output frame texture.
    TextureHandle        mFrameTexture = 0;
    int                  mFrameWidth = 0;
    int                  mFrameHeight = 0;
    std::vector<uint8_t> mFrameBuffer;
};

}  // namespace sfe
