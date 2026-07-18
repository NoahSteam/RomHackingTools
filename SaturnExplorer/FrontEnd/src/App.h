// App — the portable frontend. Owns the core context and its data source,
// holds UI state (render toggles, current selection), and draws every panel
// each frame. Depends only on the core (Seam B), ImGui, and IPlatform — no OS
// or GPU types — so it is identical across platforms.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "saturnexplorer/SaturnExplorer.h"

#include "Platform/IPlatform.h"

#ifdef SE_ENABLE_LIVE
#include "FrameRecorder.h"   // rolling capture of live frames (native only)
#endif

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

    // Load a savestate from an in-memory buffer (no filesystem); replaces any
    // context. Used by the web build, where files arrive as bytes from JS.
    bool OpenSavestateBuffer(const uint8_t* data, size_t size);

    // Connect to a running, patched Yabause (live source). 'endpoint' may be NULL
    // for the platform default socket. No-op returning false unless the build was
    // compiled with the LiveDriver (SE_ENABLE_LIVE — native desktop / Windows).
    bool OpenLive(const char* endpoint);

    // Keep trying to connect to a running Yabause on 'endpoint' (NULL = default)
    // roughly once a second until something is loaded. Call once on startup so the
    // app latches onto an emulator the moment it appears. No-op on web builds.
    void EnableLiveAutoConnect(const char* endpoint);

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
    void DrawRegisters();
    void DrawColorRam();
    void DrawWorkRam();
    void DrawVdp1Table();
    void DrawVdp2Table();
    void DrawTimeline();
    void DrawPlaceholder(const char* title, const char* note);

    // Rebuild the scrub context over the selected recorded frame (mScrubIndex).
    // Returns true when mScrubContext is valid to render from. No-op off SE_ENABLE_LIVE.
    bool RefreshScrubContext();

    // Selection helpers. mSelectedCommand is the "primary" (what the detail panels
    // show); mSelection is the full set of highlighted commands. A plain click
    // selects one; shift-click (additive) toggles a command in/out of the set.
    void SelectCommand(int command, bool additive);
    bool IsSelected(int command) const;

    se_data_source mDataSource {};
    se_context*    mContext = nullptr;
    bool           mbHasData = false;

    se_render_opts   mRenderOpts {};
    bool             mbLiveSource = false;    // data comes from a running emulator
    bool             mbPaused = false;        // live emulator held paused (frame control)
    bool             mbAutoConnectLive = false; // poll for a Yabause until one loads
    std::string      mLiveEndpoint;           // endpoint for auto-connect (empty = default)
    float            mLiveRetrySeconds = 0.0f; // time since the last connect attempt

#ifdef SE_ENABLE_LIVE
    // Rolling recording of live frames + paused-scrubbing state. While paused the
    // Timeline lets the user drag back through captured frames; the selected frame
    // is rebuilt into mScrubContext and the panels render from it for that draw.
    FrameRecorder    mRecorder;
    se_context*      mScrubContext = nullptr;  // context over the selected past frame
    bool             mbScrubbing = false;      // viewing a recorded (past) frame
    int              mScrubIndex = -1;         // selected recorded-frame index
    int              mScrubShownIndex = -1;    // index currently built into mScrubContext
#endif

    int              mSelectedCommand = -1;   // primary selection (detail panels)
    std::vector<int> mSelection;              // all selected command indices
    bool             mbLayoutBuilt = false;   // default dock layout applied once
    // Set when an external panel (VDP Output / VRAM Map / References) changes the
    // selection, so the Command List scrolls its highlighted row into view once.
    bool             mScrollCommandListToSelection = false;
    ImVec2           m3dPressPos {};          // 3D-view press point (click vs orbit)

    // Scratch buffer for the Color RAM panel, decoded once per frame.
    std::vector<se_palette_entry> mCramColors;

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
