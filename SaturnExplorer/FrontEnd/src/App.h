// App — the portable frontend. Owns the core context and its data source,
// holds UI state (render toggles, current selection), and draws every panel
// each frame. Depends only on the core (Seam B), ImGui, and IPlatform — no OS
// or GPU types — so it is identical across platforms.
#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include "saturnexplorer/SaturnExplorer.h"

#include "Platform/IPlatform.h"
#include "Settings.h"            // persistent per-user config (INI)
#include "Launcher.h"            // "Launch Session": emulator + ROM selection
#include "TopBar.h"              // top-bar view state + side-effect-free commands
#include "DataSearch.h"          // game-data-directory byte search (DataSearchHit)
#include "WatchPanel.h"          // Watch Window (debugger; emulator-agnostic)
#include "AssemblyPanel.h"       // SH-2 Assembly (debugger)
#include "HexEditorPanel.h"      // Hex Editor (debugger)
#include "ControllerPanel.h"     // Saturn control pad (drives a live game)
#include "LogPanel.h"            // structured event log (tracepoints + system events)
#include "UpdateChecker.h"       // "check GitHub for a newer build" (Seam C: IPlatform::HttpsGet)
#include "Debug/ExecutionActions.h"  // tracepoints / execution-action store
#include "Debug/CallStack.h"      // per-CPU call stack (paused-state workspace)
#include "Debug/MemoryBackend.h"
#include "Debug/WatchList.h"
#include "Debug/BreakpointManager.h"

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

    // Connect to a running, patched emulator (live source). 'endpoint' may be NULL
    // for the platform default socket. No-op returning false unless the build was
    // compiled with the LiveDriver (SE_ENABLE_LIVE — native desktop / Windows).
    bool OpenLive(const char* endpoint);

    // Keep trying to connect to a running emulator on 'endpoint' (NULL = default)
    // roughly once a second while no dump or live source is active. Call once on
    // startup so the app latches onto an emulator whenever it appears. This is a
    // background poll and does not make the rest of the UI busy. No-op on web builds.
    void EnableLiveAutoConnect(const char* endpoint);

    // Draw the whole UI. Called once per frame, between the platform's
    // BeginFrame and EndFrame.
    void BuildUI(IPlatform& platform);

private:
    void CloseData(bool cancelAutoConnect = true);
    // Persistent settings (per-user INI): panel visibility, data dir, and the
    // emulator paths the installer records. LoadSettings runs in Initialize;
    // SaveSettings on Shutdown and whenever a persisted preference changes.
    void LoadSettings();
    void SaveSettings();
    // Read every available memory region from the current source and hand a single
    // self-describing dump blob (.sedump) to the platform to save / download.
    void DumpMemory(IPlatform& platform);
    void RenderFrameToTexture(IPlatform& platform);
    void BuildDefaultLayout(unsigned int dockspaceId);
    void DrawToolbar(std::vector<TopBarCommand>& commands);
    void DrawWindowsMenu(std::vector<TopBarCommand>& commands);
    void DrawStatusBar();
    void RefreshLaunchValidation();
    TopBarViewModel BuildTopBarViewModel() const;
    void ExecuteTopBarCommand(const TopBarCommand& command, IPlatform& platform);
    void DrawRecordingSettingsModal();
    void DrawSettingsModal();
    void DrawHelpModal();
    void DrawAboutModal();
    void DrawUpdateModal(IPlatform& platform);   // "Check for Updates" result (polls mUpdateChecker)
    void SaveScreenshot(IPlatform& platform);
    void DrawLayersMenu();   // toolbar "Layers" dropdown (VDP1/VDP2 visibility toggles)
    void DrawVdpOutput(IPlatform& platform);
    void DrawWatch(IPlatform& platform);   // debugger Watch Window
    void DrawAssembly();                    // SH-2 Assembly (live disassembly)
    void DrawHexEditor();                   // Hex Editor (memory view/edit)
    void DrawController(IPlatform& platform); // Saturn control pad -> live input
    void SendInput(unsigned int mask);      // push a pad mask to the live emulator (on change)
    void DrawLog();                         // structured event log
    void DrawActions();                     // Tracepoints management table
    void DrawCallStack();                   // per-CPU call stack (paused-state workspace)
    void DrawBreakpoints();                 // Visual Studio-style breakpoint list
    void DrawSound(IPlatform& platform);    // SCSP voices: who's playing + Play/Export
    void ExportSound(IPlatform& platform, int slot);   // decode voice 'slot' -> save .wav
    void RebuildCallStack();                // reconstruct the shown CPU's stack
    // Sync the workspace to a selected call-stack frame (Assembly + Hex + focus).
    void GoToFrame(const CallStackFrame& fr);
    void DrawTracepointEditor();            // modal property editor for a tracepoint
    void OpenTracepointEditor(int cpu, uint32_t addr);  // open it for a new/existing TP
    // Format a tracepoint's template against the CURRENT context (registers + memory),
    // for the editor's live preview and Test Fire. Empty string if no context.
    std::string FormatAgainstContext(const std::string& tmpl, int cpu);
    void SyncBreakpointsToLive();           // push the breakpoint set to the emulator
    // Instruction stepping (from the paused/breakpoint-hit workspace). StepInto runs one
    // SH-2 instruction; StepOver runs a called subroutine to completion (else one instr);
    // StepOut runs to the current frame's return address (PR). cpu = the halted CPU; each
    // reads that CPU's registers itself, so callers just pass the CPU.
    void StepInto(int cpu);
    void StepOver(int cpu);
    void StepOut(int cpu);
    // Install the transient step breakpoint at 'addr' and resume — the shared "run to a
    // computed address, then halt" used by StepOver/StepOut. CPU-agnostic (SH-2 PC
    // breakpoints are shared across both cores).
    void RunToTransient(uint32_t addr);
    // Resume the halted emulator (shared by the toolbar, both run-control strips, Run to
    // Here, and the step helpers). No-op without live frame control.
    void Continue();
    void SyncTracepointsToLive();           // push the tracepoint set to the emulator (v8)
    void DrainTraceEvents();                // pull fired tracepoint events into the Log
    void DrawVdp1Framebuffer(IPlatform& platform);
    void DrawWorldView(IPlatform& platform);
    void DrawCommandList();
    void DrawSelectedObject();
    void DrawTextureViewer(IPlatform& platform);
    // Add a write watchpoint over the VDP1 VRAM bytes a command's texture occupies, so the
    // emulator halts when the CPU overwrites that texture. False if it has no footprint.
    bool BreakOnTextureWrite(const se_command& cmd);
    // Export the currently-shown texture as a .bmp (paletted BMP with the game's
    // palette when the texture is paletted, else 24-bit) via the platform save dialog.
    void ExportTexture(IPlatform& platform, const se_command& cmd, int w, int h);
    // Game-data-directory search: pick/show the data dir, kick a texture search, and
    // draw its results. If no dir is set, BeginTextureSearch stashes the needle and
    // pops the set-dir modal, which runs the pending search once a dir is chosen.
    void DrawDataDirModal(IPlatform& platform);
    // "Launch Session": the nested toolbar Launch menu (pick emulator + ROM), the
    // Launch Settings dialog (per-emulator exe/args/workdir), and the launch action
    // (resolve exe+args and hand them to the platform; adopt the ROM as the Data
    // Directory when none is set yet).
    void DrawSessionMenu(const TopBarViewModel& state, std::vector<TopBarCommand>& commands);
    void DrawLaunchSettingsModal(IPlatform& platform);
    bool LaunchSession(IPlatform& platform, bool connectAfterLaunch);
    void BeginTextureSearch(IPlatform& platform, const se_command& cmd);
    // Search the game data directory for an arbitrary byte sequence (the Hex Editor's
    // selection, or the SH-2 Assembly panel's selected instructions). Stashes the needle
    // and either runs immediately or opens the "set data directory" modal first. Results
    // land in the shared "Data Search Results" window.
    void BeginByteSearch(std::vector<uint8_t> needle, const std::string& label);
    void RunPendingSearch();
    // "Search Options..." — the dialog that chooses the search scope (files/dirs) and the
    // compression type (raw bytes, or a PRS-compressed block), opened from the texture
    // right-click. Stashes the current texture as the needle, then runs on Save & Search.
    void BeginTextureSearchOptions(const se_command& cmd);
    void DrawSearchOptionsModal(IPlatform& platform);
    // Build the search needle (a texture's raw packed VRAM bytes) + a human label. False if
    // there is no data or the texture has no footprint. Shared by both search entry points.
    bool BuildTextureNeedle(const se_command& cmd, std::vector<uint8_t>& needle,
                            std::string& label);
    // Launch the pending search (mPendingNeedle) over `roots` with compression `comp` on a
    // worker thread. A PRS scan can be slow, so it must not block the UI. `scopeText`
    // describes what is being searched, for the results summary.
    void LaunchSearch(std::vector<std::string> roots, SearchCompression comp,
                      const std::string& scopeText);
    void PollSearchWorker();   // called each frame: joins the finished worker
    void LoadSearchOptions();
    void SaveSearchOptions();
    void DrawDataSearchResults(IPlatform& platform);
    // Resolve a command's palette (CLUT or CRAM bank); SE_ERR_UNSUPPORTED for RGB555.
    se_result PaletteOf(const se_command& cmd, se_palette* pal);
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
    void DrawTransportBar();   // prev/play/scrub/next, at the bottom of the VDP Output view
    void DrawPlaceholder(const char* title, const char* note);

    // Rebuild the scrub context over the selected recorded frame (mScrubIndex).
    // Returns true when mScrubContext is valid to render from. No-op off SE_ENABLE_LIVE.
    bool RefreshScrubContext();

    // Selection helpers. mSelectedCommand is the "primary" (what the detail panels
    // show); mSelection is the full set of highlighted commands. A plain click
    // selects one; shift-click (additive) toggles a command in/out of the set.
    void SelectCommand(int command, bool additive);
    bool IsSelected(int command) const;
    // Reveal the current selection in both index tables (Command List + VDP1 Table);
    // used by the non-table panels (2D/3D views, VRAM Map) that select a command.
    void RevealSelectionInTables();

    se_data_source mDataSource {};
    se_context*    mContext = nullptr;
    bool           mbHasData = false;
    SourceState    mSource;

    // Debugger panels (emulator-agnostic: they read through the backend interface,
    // which is served here from the current se_context — live snapshot or scrub).
    ContextBackend           mMemBackend{&mContext};
    SimpleExpressionResolver mExprResolver;
    WatchPanel               mWatchPanel;
    BreakpointManager        mBreakpoints;
    AssemblyPanel            mAssemblyPanel;
    HexEditorPanel           mHexEditor;
    ControllerPanel          mController;
    unsigned int             mInputMask = 0;    // last pad mask sent to the live emulator
    bool                     mLogInput = false; // log each transmitted pad mask to the Log window
    uint64_t                 mControllerFrame = 0; // live frame (never scrub-context frame)

    // Structured event log + the tracepoint (execution-action) store, plus the state
    // of the modal tracepoint editor (mTpEdit is the working copy; mTpEditNew means
    // "Add on OK" vs "Update the existing id").
    LogPanel                 mLog;
    ExecutionActions         mActions;
    bool                     mTpEditorOpen = false;
    bool                     mTpEditNew = false;
    ExecutionAction          mTpEdit;
    // Format-field autocomplete state (editor only). mTpFmtCursor tracks the InputText
    // caret (updated from its callback); mTpFmtForce re-seeds the buffer + caret from
    // mTpEdit.format after a suggestion is inserted; mTpFmtRefocus re-focuses the field.
    int                      mTpFmtCursor = 0;
    bool                     mTpFmtForce = false;
    bool                     mTpFmtRefocus = false;
    uint64_t                 mLastSystemLogFrame = ~0ull;   // de-dupe per-frame system logs
    uint64_t                 mLastTpGeneration = 0;         // last tracepoint set synced live
    uint64_t                 mLastBpGeneration = 0;  // last set synced to the live emulator

    // Call stack (paused-state workspace). Rebuilt when execution stops or a savestate
    // loads; mCallStackDirty flags a needed rebuild, mCallStackCpu picks the CPU shown,
    // and the rename popup edits a function name at mRenameAddr.
    CallStack                mCallStack;
    FunctionNames            mFunctionNames;
    bool                     mCallStackDirty = true;
    bool                     mFocusCallStack = false;   // bring the panel forward on a stop
    bool                     mCallStackWasShowable = false;  // edge-detect entering paused/loaded
    int                      mCallStackCpu = 0;
    bool                     mRenameOpen = false;
    uint32_t                 mRenameAddr = 0;
    char                     mRenameBuf[64] = {};

    se_render_opts   mRenderOpts {};
    bool             mbLiveSource = false;    // data comes from a running emulator
    bool             mbPaused = false;        // live emulator held paused (frame control)
    // Live breakpoint-hit state, mirrored to the Assembly panel so it tints the halted row.
    bool             mBpStopActive = false;
    int              mBpStopCpu = 0;
    uint32_t         mBpStopPc = 0;
    // Transient (one-shot) step breakpoint for Step Over / Step Out: a run-to-address the
    // client installs alongside the user set and removes automatically once hit. Kept out
    // of BreakpointManager so it never shows in the gutter.
    bool             mStepBpActive = false;
    uint32_t         mStepBpAddr = 0;
    bool             mStepBpDirty = false;   // forces a breakpoint re-sync when it changes
    bool             mbAutoConnectLive = false; // poll while no dump/live source is active
    std::string      mLiveEndpoint;           // endpoint for auto-connect (empty = default)
    float            mLiveRetrySeconds = 0.0f; // time since the last connect attempt
    std::string      mOperationStatus;
    bool             mOperationError = false;

#ifdef SE_ENABLE_LIVE
    // Rolling recording of live frames + paused-scrubbing state. While paused the
    // Timeline lets the user drag back through captured frames; the selected frame
    // is rebuilt into mScrubContext and the panels render from it for that draw.
    FrameRecorder    mRecorder;
    int              mRecordSeconds = 5;       // ring-buffer window (5..30 s)
    bool             mbRecording = false;      // explicit recording state
    double           mRecordingStartedAt = 0.0;
    se_context*      mScrubContext = nullptr;  // context over the selected past frame
    bool             mbScrubbing = false;      // viewing a recorded (past) frame
    int              mScrubIndex = -1;         // selected recorded-frame index
    int              mScrubShownIndex = -1;    // index currently built into mScrubContext
    se_context*      mLiveCtx = nullptr;       // the live context, reachable while panels
                                               // render from the scrub context (transport)
#endif

    // Game data directory (a folder of the game's extracted files, or an ISO/disc
    // image) that the texture "Find in game data" search scans. Persisted only in
    // memory for the session. The set-dir modal opens when the user asks for it, or
    // automatically when a search is requested with no directory set yet.
    std::string          mDataDir;
    bool                 mOpenDataDirModal = false;   // request to open the modal
    bool                 mSearchAfterSetDir = false;  // run pending search once dir set
    std::vector<uint8_t> mPendingNeedle;              // texture bytes to search for
    std::string          mPendingSearchLabel;         // human label for the search
    bool                 mShowSearchResults = false;
    std::vector<DataSearchHit> mSearchResults;
    std::string          mSearchSummary;              // "<label>: N match(es) in M file(s)"

    // "Search Options..." configuration (persisted): where to search and whether the
    // texture is expected raw or inside a PRS-compressed block. Empty `paths` => fall back
    // to the game data directory above.
    struct SearchOptions
    {
        SearchCompression        compression = SearchCompression::None;
        std::vector<std::string> paths;   // files and/or directories to search
    };
    SearchOptions        mSearchOptions;
    bool                 mOpenSearchOptions = false;  // request to open the modal

    // Async search worker. A PRS scan tries to decompress at every offset of every file, so
    // it can take a while; it runs off the UI thread with live progress + cancellation.
    std::thread          mSearchThread;
    SearchProgress       mSearchProgress;             // worker <-> UI (atomics)
    std::atomic<bool>    mSearchRunning{false};       // a worker is active
    std::atomic<bool>    mSearchDone{false};          // worker finished; results ready to reap
    std::string          mSearchScopeText;            // human description of what was searched

    // Per-panel visibility, toggled from the toolbar "Windows" menu. All shown by
    // default; a hidden panel simply isn't drawn (its dock tab disappears until
    // re-enabled). Session-only state — these reset to visible each launch.
    struct Panels
    {
        // Archive Explorer + Search ROM/Files are M6 placeholders, and References is
        // niche — hidden by default (re-enable from the Windows menu).
        bool vramMap = true, archiveExplorer = false, searchRom = false;
        bool vdpOutput = true, vdp1Framebuffer = true, worldView = true;
        bool vdp1Table = true, vdp2Table = true, colorRam = true, workRam = true;
        bool registers = true, commandList = true;
        bool textureViewer = true, paletteViewer = true, references = false;
        bool selectedObject = true, watch = true, assembly = true, hexEditor = true;
        bool controller = true;   // Saturn control pad (drives a live game)
        bool log = true;          // structured event log (tracepoints + system events)
        bool actions = true;      // Tracepoints / execution-actions management table
        bool callStack = true;    // per-CPU call stack (paused-state workspace)
        bool breakpoints = true;  // Visual Studio-style breakpoint list (tabs by Call Stack)
        bool sound = true;        // SCSP voices (live): who's playing + Play/Export
    };
    Panels           mPanels;

    // The single source of truth for every toggleable panel: its settings key, its
    // Windows-menu label, and a pointer to its visibility flag. The Windows menu and
    // settings load/save all iterate this one list, so a new panel is added in
    // exactly one place instead of three parallel enumerations.
    struct PanelInfo { const char* key; const char* label; bool Panels::* flag; };
    static const std::vector<PanelInfo>& PanelList();

    // Persistent settings + the layout ini path (imgui.ini relocated into the
    // per-user config dir so the dock layout survives regardless of the working
    // directory). mIniPath backs ImGuiIO::IniFilename, so it must outlive the
    // ImGui context — hence a member, not a local. mSettingsDirty triggers a save
    // at end of frame after the user changes a persisted preference.
    Settings         mSettings;
    std::string      mIniPath;
    bool             mSettingsDirty = false;
    // "Launch Session": which emulator + ROM the toolbar's Launch menu starts. Exe
    // paths come from the installer ([emulators] in settings) and are user-overridable
    // in Launch Settings. Owns the recent-ROM list + the set-data-dir coupling.
    Launcher         mLauncher;
    LaunchValidation mLaunchValidation;
    bool             mOpenLaunchSettings = false;    // request to open the Launch Settings modal
    bool             mLaunchSettingsInit = false;    // (re)load edit buffers on modal open
    bool             mOpenRecordingSettings = false;
    bool             mOpenSettings = false;
    bool             mOpenHelp = false;
    bool             mOpenAbout = false;
    bool             mOpenUpdate = false;   // request to open the "Check for Updates" modal
    UpdateChecker    mUpdateChecker;
    // Edit buffers for the Launch Settings dialog (ImGui InputText needs char storage;
    // no imgui_stdlib here). Parallel to mLauncher.Emulators(); copied in on open,
    // written back on Save.
    struct LaunchEdit { char exe[512]; char args[256]; char workDir[512]; };
    std::vector<LaunchEdit> mLaunchEdits;
    int              mLaunchSelectedEdit = 0;
    bool             mLaunchSetDataDirEdit = true;

    int              mSelectedCommand = -1;   // primary selection (detail panels)
    std::vector<int> mSelection;              // all selected command indices
    bool             mbLayoutBuilt = false;   // default dock layout applied once
    bool             mForceRebuildLayout = false;  // "Reset Layout" -> rebuild the default
    // Set when an external panel (VDP Output / VRAM Map / References) changes the
    // selection, so the Command List scrolls its highlighted row into view once.
    bool             mScrollCommandListToSelection = false;
    // Same, for the VDP1 Table panel (set when a command is picked elsewhere and the
    // table should scroll+surface that row).
    bool             mScrollVdp1TableToSelection = false;
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

    // VDP1 Framebuffer viewer (decodes the captured 512x256 front bank).
    TextureHandle        mFbTexture = 0;
    int                  mFbTexW = 0;
    int                  mFbTexH = 0;
    std::vector<uint8_t> mFbRaw;                  // captured bytes (big-endian words)
    std::vector<uint8_t> mFbRgba;                 // decoded RGBA8888
    std::vector<se_palette_entry> mFbCram;        // CRAM lookup, rebuilt each frame
    int                  mFbMode = 0;             // 0=Resolved, 1=Raw RGB555, 2=Priority
    bool                 mFbByteSwap = false;     // flip 16-bit word endianness
};

}  // namespace sfe
