// App — the portable frontend. Owns the core context and its data source,
// holds UI state (render toggles, current selection), and draws every panel
// each frame. Depends only on the core (Seam B), ImGui, and IPlatform — no OS
// or GPU types — so it is identical across platforms.
#pragma once

#include <cstdint>
#include <vector>

#include "saturnexplorer/SaturnExplorer.h"

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

    // Load Saturn state from a Yabause savestate (.yss); replaces any context.
    bool OpenSavestate(const char* path);

    // Draw the whole UI. Called once per frame, between the platform's
    // BeginFrame and EndFrame.
    void BuildUI(IPlatform& platform);

private:
    void CloseData();
    void RenderFrameToTexture(IPlatform& platform);
    void BuildDefaultLayout(unsigned int dockspaceId);
    void DrawToolbar(IPlatform& platform);
    void DrawStatusBar();
    void DrawLayerControls();
    void DrawVdpOutput(IPlatform& platform);
    void DrawWorldView(IPlatform& platform);
    void DrawCommandList();
    void DrawSelectedObject();
    void DrawTextureViewer(IPlatform& platform);
    void DrawPaletteViewer();
    void DrawPaletteSwatches(const se_palette& pal);
    void DrawVramMap();
    void DrawReferences();
    void DrawReferenceList(const char* id, const std::vector<se_reference>& refs);
    void DrawPlaceholder(const char* title, const char* note);

    se_data_source mDataSource {};
    se_context*    mContext = nullptr;
    bool           mbHasData = false;

    se_render_opts mRenderOpts {};
    int            mSelectedCommand = -1;
    bool           mbLayoutBuilt = false;   // default dock layout applied once

    // VDP Output frame texture.
    TextureHandle        mFrameTexture = 0;
    int                  mFrameWidth = 0;
    int                  mFrameHeight = 0;
    std::vector<uint8_t> mFrameBuffer;

    // 3D View texture + orbit camera.
    TextureHandle        m3dTexture = 0;
    int                  m3dWidth = 0;
    int                  m3dHeight = 0;
    std::vector<uint8_t> m3dBuffer;
    float                mYaw = 0.6f;
    float                mPitch = 0.4f;
    float                mDistance = 520.0f;

    // Texture Viewer (decodes the selected sprite's texture each frame).
    TextureHandle        mTexTexture = 0;
    int                  mTexWidth = 0;
    int                  mTexHeight = 0;
    std::vector<uint8_t> mTexBuffer;
};

}  // namespace sfe
