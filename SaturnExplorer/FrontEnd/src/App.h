// App — the portable frontend. Owns the core context and its data source,
// holds UI state (render toggles, current selection), and draws every panel
// each frame. Depends only on the core (Seam B), ImGui, and IPlatform — no OS
// or GPU types — so it is identical across platforms.
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "saturnexplorer/SaturnExplorer.h"

#include "Platform/IPlatform.h"
#include "Settings.h"            // persistent per-user config (INI)
#include "Launcher.h"            // "Launch Session": emulator + ROM selection
#include "TopBar.h"              // top-bar view state + side-effect-free commands
#include "Demo/DemoPlayer.h"     // in-app feature-tour playback (drives panels for recording)
#include "DataSearch.h"                 // game-data-directory byte search (DataSearchHit)
#include "DataSearchRunner.h"           // its worker thread, queueing and result routing
#include "WatchPanel.h"          // Watch Window (debugger; emulator-agnostic)
#include "AssemblyPanel.h"       // SH-2 Assembly (debugger)
#include "Sh2Dump.h"             // Data > Dump SH-2: the listing as text
#include "HexEditorPanel.h"      // Hex Editor (debugger)
#include "CompareMarkers.h"      // Frame Memory Compare: the two timeline markers
#include "ScrubState.h"          // frame-identity rules for the scrub view and staged edits
#include "MemoryComparePanel.h"  // Frame Memory Compare: the diff window
#include "ControllerPanel.h"     // Saturn control pad (drives a live game)
#include "LogPanel.h"            // structured event log (tracepoints + system events)
#include "LayerPanels.h"       // per-layer viewer tabs (VDP1 / NBG0-3 / RBG0)
#include "UpdateChecker.h"       // "check GitHub for a newer build" (Seam C: IPlatform::HttpsGet)
#include "Debug/ExecutionActions.h"  // tracepoints / execution-action store
#include "Debug/CallStack.h"      // per-CPU call stack (paused-state workspace)
#include "Debug/MemoryBackend.h"
#include "RenderKey.h"
#include "Debug/MemorySearch.h"   // Cheat-Engine-style live RAM value scanner
#include "Debug/MemorySearchRunner.h"   // ...and the worker that keeps its scans off the UI thread
#include "Debug/AccessLog.h"      // "find what accesses this address" record
#include "Disc/DiscImage.h"       // disc-image reader + ISO 9660 browser (sector -> file)
#include "Debug/WatchList.h"
#include "Debug/BreakpointManager.h"
#include "Debug/StepHaltMachine.h"

#ifdef SE_ENABLE_LIVE
#include "FrameRecorder.h"
#include "SavestateSlots.h" // numbered save states (native only)
#include "SeLiveProtocol.h"  // SE_LIVE_EMU_SLOTS (the emulator's own slot count)
#include "PatchLibrary.h"    // Patch feature: known memory->file locations + patch-script emit
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
    // Turn a SaveOutcome into the operation banner + log line. Returns true only for a real
    // save, so a caller can skip whatever it would have said about the file afterwards.
    bool ReportSave(SaveOutcome outcome, const std::string& what);
    // Complain loudly if a save of the displayed frame is running before that frame has been
    // chosen. See the definition; 'what' names the feature for the message.
    void WarnIfNotFromDisplayedSnapshot(const char* what);
    // Queue Data > Dump Textures for the layer 'layer' (a LayerId). Run by LayerPanels::Draw,
    // not here, so it exports the frame on screen rather than the live one -- see the comment
    // on the definition.
    void RequestTextureDump(int layer);
    // Data > Dump SH-2: write the SH-2 disassembly the Assembly panel shows to a text file. The
    // dialog picks the columns and memory; the job then runs a slice per frame under a progress
    // modal, and the finished text goes to the platform's save dialog.
    void DrawDumpSh2Modal(IPlatform& platform);
    void BeginDumpSh2();
    void RenderFrameToTexture(IPlatform& platform);
    void BuildDefaultLayout(unsigned int dockspaceId);
    void DrawToolbar(std::vector<TopBarCommand>& commands);
    void DrawWindowsMenu(std::vector<TopBarCommand>& commands);
    // The ImGui toolbar's stand-in for the native bar's Data > Dump Textures submenu. The SDL
    // backends draw the toolbar instead of an OS menu bar, so without this the feature would
    // be reachable on Windows and macOS only.
    void DrawDumpTexturesMenu(const TopBarViewModel& state, std::vector<TopBarCommand>& commands);
    void DrawStatusBar();
    void RefreshLaunchValidation();
    TopBarViewModel BuildTopBarViewModel() const;
    void ExecuteTopBarCommand(const TopBarCommand& command, IPlatform& platform);
    // Keyboard shortcuts shared by both front ends (the ImGui toolbar and the native Win32 menu
    // bar). Runs every frame regardless of which bar is drawn, so hotkeys work on Windows even
    // though the ImGui toolbar isn't.
    void CollectToolbarShortcuts(std::vector<TopBarCommand>& commands, const TopBarViewModel& state);
    // Native OS menu-bar bridge (used only under SE_NATIVE_MENUBAR). BuildNativeMenuState mirrors
    // the toolbar state for the platform to render; DispatchNativeMenuAction maps a selection onto
    // the same TopBarCommand queue (or the few view-only toggles the toolbar drives inline).
    NativeMenuState BuildNativeMenuState(const TopBarViewModel& state) const;
    void DispatchNativeMenuAction(const NativeMenuAction& action, std::vector<TopBarCommand>& commands);
    void ToggleMenuLayer(int layer);   // flip one se_render_opts field by NativeMenuLayer index
    void DrawRecordingSettingsModal();
    void DrawDiffSettingsModal(IPlatform& platform);   // Settings > Diff...: the external diff tool
    void LaunchExternalDiff(IPlatform& platform);      // frames A and B -> folders -> the tool
    void DrawHelpModal();
    void DrawAboutModal();
    void DrawUpdateModal(IPlatform& platform);   // "Check for Updates" result (polls mUpdateChecker)
    void SaveScreenshot(IPlatform& platform);
    bool mScreenshotRequested = false;   // taken once the displayed context is selected
    bool mDumpMemoryRequested = false;   // likewise -- a dump of the frame on screen
    bool mDispatchingCommands = false;   // inside the toolbar/menu command loop (see the guard)
    void DrawLayersMenu();   // toolbar "Layers" dropdown (VDP1/VDP2 visibility toggles)
    void DrawVdpOutput(IPlatform& platform);
    void AdoptNewPanels(ImGuiID dockId);
    void DrawLayerPanels(IPlatform& platform);   // per-layer viewer tabs (VDP1 / NBG / RBG0)
    void DrawWatch(IPlatform& platform);   // debugger Watch Window
    void DrawAssembly();                    // SH-2 Assembly (live disassembly)
    void DrawHexEditor();                   // Hex Editor (memory view/edit)
    void DrawController(IPlatform& platform); // Saturn control pad -> live input
    void SendInput(unsigned int mask);      // push a pad mask to the live emulator (on change)
    void DrawLog();                         // structured event log
    void DrawActions();                     // Tracepoints management table
    void DrawCallStack(IPlatform& platform);// per-CPU call stack (paused-state workspace)
    void DrawBreakpoints();                 // Visual Studio-style breakpoint list
    void DrawRamSearch();                   // Cheat-Engine-style live RAM value scanner
    void DrawAccessLog();                   // "find what accesses this address"
    void DrawSoundCpu();                    // SCSP 68000 sound-CPU disassembly (Sound RAM)
    void DrawDiscExplorer(IPlatform& platform); // disc image ISO browser (sector -> file)
    bool FetchSh2Instruction(uint32_t pc, DisassembledInstruction& ins);
    BreakpointManager::WatchCauses WatchCausesAtHalt(int cpu, uint32_t pc);
    void RecordAccess(int cpu, uint32_t pc);// file a data-watchpoint hit into the access log
    void DrawSound(IPlatform& platform);    // SCSP voices: who's playing + Play/Export
    void ExportSound(IPlatform& platform, int slot);   // decode voice 'slot' -> save .wav
    void AddAddressWatch(const char* prefix, uint32_t addr, WatchType type);
    void PlaySound(IPlatform& platform, int slot);     // decode voice 'slot' -> preview audio
    void PlaySoundFrame(IPlatform& platform);          // mix every sounding voice -> preview
    // Output format for the frame mix. 44.1 kHz is the SCSP's own base rate, so the
    // common case of an unpitched voice resamples 1:1; the cap bounds a preview at 4s.
    static const uint32_t kFrameMixRate = 44100;
    static const size_t   kFrameMixMaxFrames = 44100 * 4;
    // Decode a voice's sample from sound RAM into 'out' (16-bit mono); returns frame count,
    // fills 'rate' with the natural playback rate. Shared by Play + Export.
    int  DecodeSlotSample(int slot, std::vector<int16_t>& out, uint32_t& rate);
    void RebuildCallStack();                // reconstruct the shown CPU's stack
    // Fill 'out' with 'cpu's call stack: the emulator's ● Confirmed shadow stack when the
    // live source has one, else a heuristic reconstruction. Shared by the Call Stack panel
    // and the Access Log so both prefer the confirmed frames.
    void BuildCallStack(int cpu, const se_sh2_regs& regs, CallStack& out);
    // Sync the workspace to a selected call-stack frame (Assembly + Hex + focus).
    void GoToFrame(const CallStackFrame& fr);
    // A call-stack frame's display name, and the code address the UI should act on for it.
    // Split out because a heuristic frame often has no known function entry (CPU-03) and every
    // place that used to read functionAddress directly would otherwise name, navigate to, or
    // set a breakpoint on address zero.
    std::string FrameLabel(const CallStackFrame& fr) const;
    static uint32_t FrameCodeAddress(const CallStackFrame& fr);
    void DrawTracepointEditor();            // modal property editor for a tracepoint
    void OpenTracepointEditor(int cpu, uint32_t addr);  // open it for a new/existing TP
    // Format a tracepoint's template against the CURRENT context (registers + memory),
    // for the editor's live preview and Test Fire. Empty string if no context.
    std::string FormatAgainstContext(const std::string& tmpl, int cpu);
    // Evaluate a breakpoint/tracepoint guard (ConditionEval syntax) against 'cpu's current
    // registers + memory. Empty guard, or no live data, returns true.
    bool EvalCondition(const std::string& cond, int cpu);
    void SyncBreakpointsToLive();           // push the breakpoint set to the emulator
    // Instruction stepping (from the paused/breakpoint-hit workspace). StepInto runs one
    // SH-2 instruction; StepOver runs a called subroutine to completion (else one instr);
    // StepOut runs to the current frame's return address, taken from the recorded call frame (PR
    // is not it once the function has made a call of its own). All three apply to the CPU that
    // HALTED, whatever 'cpu' a panel passes: the instruction step runs the CPU the stop latched, and
    // a transient breakpoint is completed only by that CPU. The panels disable the buttons while
    // they show the other one.
    void StepInto(int cpu);
    void StepOver(int cpu);
    void StepOut(int cpu);
    int  SteppedCpu() const;
    // Install the transient step breakpoint at 'addr' and resume — the shared "run to a
    // computed address, then halt" used by StepOver/StepOut. The breakpoint itself is shared across
    // both SH-2 cores (that is how the emulator holds it); the step completes only when 'cpu' reaches
    // it.
    void RunToTransient(uint32_t addr, int cpu);
    // Resume the halted emulator (shared by the toolbar, both run-control strips, Run to
    // Here, and the step helpers). No-op without live frame control.
    void Continue();
    void SyncTracepointsToLive();           // push the tracepoint set to the emulator (v8)
    void DrainTraceEvents();                // pull fired tracepoint events into the Log
    void DrawWorldView(IPlatform& platform);
    void DrawCommandList();
    // Inline size/position editing in the Command List: draw an editable integer cell for one
    // command field and, on commit, re-encode it into the command's CMDSIZE/CMDXA/CMDYA word
    // and write it back to VDP1 VRAM (which pokes a live emulator). Returns true if it changed.
    bool EditCommandSize(const se_command& cmd);
    bool EditCommandPosition(const se_command& cmd);
    void PushCommandEditId(const se_command& cmd);   // pushes two IDs; pop both
    void WriteCommandWord(const se_command& cmd, uint32_t fieldOffset, uint16_t value);
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
    // Toolbar "State" menu: save to / load from the numbered slots.
    void DrawStateMenu(const TopBarViewModel& state, std::vector<TopBarCommand>& commands);
    void DoSaveState(int slot);
    void DoLoadState(int slot);
    void DoLoadEmulatorState(int slot);
    // Refresh the emulator's own save-slot inventory from the live driver. Cheap; polled
    // each frame so the menu reflects a state saved in the emulator while SE is attached.
    void RefreshEmulatorSlots();
    void DrawLaunchSettingsModal(IPlatform& platform);
    // Start the selected emulator. With a non-empty romOverride, launch THAT disc instead of the
    // selected ROM without changing the user's selection (used by Build & Launch ISO).
    bool LaunchSession(IPlatform& platform, const std::string& romOverride = std::string());
    void BeginTextureSearch(IPlatform& platform, const se_command& cmd);
    // Which window a search's results land in. Travels with the request through the runner
    // (DataSearchRequest::destination) rather than sitting in a flag per search slot.
    enum SearchDestination
    {
        kSearchToTextureResults = 0,   // "Find in game data" -> the Data Search Results window
        kSearchToLocateResults = 1,    // Patch "Find in game files" -> accept/reject rows
    };

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
    // Hand the pending search (mPendingNeedle) to mSearchRunner: `roots` is what to scan,
    // `comp` whether the bytes are raw or inside a PRS block, `scopeText` a description for the
    // summary, and `destination` which window gets the results (SearchDestination). The runner
    // owns the thread and the cancel-and-queue behaviour; this only builds the request and
    // decides what a request with nothing to search means.
    void LaunchSearch(std::vector<std::string> roots, SearchCompression comp,
                      const std::string& scopeText,
                      int destination = kSearchToTextureResults);
    // Called each frame: take a finished search's results from the runner and route them.
    void PollSearchWorker();
    void LoadSearchOptions();
    void SaveSearchOptions();
    void DrawDataSearchResults(IPlatform& platform);
    // Resolve a command's palette (CLUT or CRAM bank); SE_ERR_UNSUPPORTED for RGB555.
    se_result PaletteOf(const se_command& cmd, se_palette* pal);
    void DrawPaletteViewer();
    // 'baseAddress' is where the palette lives on the Saturn bus, so double-clicking the
    // grid can reveal it in the Memory panel; 0 means unknown and disables that.
    void DrawPaletteSwatches(const se_palette& pal, uint32_t baseAddress);
    void DrawVramMap();
    void DrawReferences();
    void DrawReferenceList(const char* id, const std::vector<se_reference>& refs);
    void DrawRegisters();
    void DrawSh2Registers();   // the Registers panel's "SH-2" tab
    void DrawColorRam();
    void DrawVdp1Table();
    void DrawVdp2Table();
    void DrawTransportBar();   // prev/play/scrub/next, at the bottom of the VDP Output view
    bool MarkCompareFrame(CompareMarkers::Slot slot);   // snapshot the shown frame as marker A or B
    void OpenCompare();                                 // diff the two markers and show the panel
    void ClearCompare();                                // drop the markers and the diff
    void ResetCompareSession();                         // a different emulator run or source
    int  CompareMarkerIndex(CompareMarkers::Slot slot) const;     // a marker's place on the timeline, or -1
    uint64_t CompareDiffSideFrame(CompareMarkers::Slot slot) const;   // the shown comparison's frame if it is on the timeline, else 0
    void ExportCompareDiff(const MemoryComparePanel::Request& req);
    void PumpCompareExport(IPlatform& platform);
    void CancelCompareExport();
    bool CompareRowVisible() const;
    void DrawCompareMarkers(const ImVec2& sliderMin, const ImVec2& sliderMax, int frameCount);
    void DrawCompareRow();
    void CompareRowMetrics(char (&text)[2][64], float (&width)[3]) const;   // the row's labels and item widths
    float CompareRowHeight(float availWidth) const;                         // what the row takes at that width, 0 if hidden
    void DrawMemoryCompare(IPlatform& platform);
    void HandleCompareRequest(const MemoryComparePanel::Request& req, IPlatform& platform);
    bool ScrubToFrame(uint64_t frameNo);                // pause if needed and show recorded frame 'frameNo'
    bool EmulatorStampsStates() const;   // protocol v22+: Play From Here is only safe against these
    int  PlayFromHereTarget() const;
    std::string PlayFromHereTooltip(se_context* ctl, int target, bool canPlayHere) const;
    void PlayFromScrubbedFrame(se_context* ctl);   // restore the scrubbed frame, drop what followed
    void DrawPlaceholder(const char* title, const char* note);

    // --- Feature-tour Demo Mode: play a .sedemo script that drives the real UI for a screen
    // recording (narration is read separately; nothing is captioned on screen). DrawDemoMenu
    // is the toolbar dropdown; UpdateDemo (called once per frame, with the platform for the
    // file dialog + safe commands) services the hotkeys/requests, ticks the player, and
    // applies a beat when it becomes current; DrawDemoOverlay is the operator HUD.
    void DrawDemoMenu();
    void UpdateDemo(IPlatform& platform);
    void ApplyDemoBeat(const DemoBeat& beat, IPlatform& platform);
    void DrawDemoOverlay();
    void LoadDemoScript(const std::string& path, IPlatform& platform);
    // Set one panel's visibility by its PanelList key; false if the key is unknown.
    bool SetPanelVisible(const std::string& key, bool visible);
    // Toggle a named render layer (vdp1/wireframe/bounds/objnums/nbg0..3/rbg0/window/
    // colorcalc/shadow) on or off; false if the name is unknown.
    bool SetRenderLayer(const std::string& name, bool on);

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
    int                      mRestoreOutstanding = 0;   // load requests the emulator has not yet resolved
    // Savestate blocks of an epoch older than this belong to a timeline a state load has since
    // abandoned (see OnStateBlock). The count the emulator will report once every load submitted
    // so far has landed; 0 accepts everything.
    uint32_t                 mBlockEpochFloor = 0;
    uint32_t                 mRestoreBaseDone = 0, mRestoreBaseFailed = 0;   // counters when the first was sent
    float                    mRestoreWaitSeconds = 0.0f;
    bool                     mRestoreTimedOut = false;      // reported once; edits stay refused
    bool                     mRestoreUnconfirmable = false; // a load went to an emulator that cannot confirm it
    SimpleExpressionResolver mExprResolver;
    WatchPanel               mWatchPanel;
    BreakpointManager        mBreakpoints;
    // Cached per-BP guard validation error (id -> message), refreshed only when the
    // condition text is edited so the Breakpoints panel isn't re-parsing every frame.
    std::unordered_map<uint64_t, std::string> mBpCondErrors;

    // RAM Search (Cheat-Engine-style value scanner) — engine + its panel's UI state.
    MemorySearch     mRamSearch;
    MemorySearchRunner mRamSearchRunner;
    void ResetSessionDebugState();   // RAM search, access log, tracepoint sync + counts (any build)
    int              mRamSearchType = 2;      // index into the panel's type list (default u16)
    int              mRamSearchCmp = 0;       // index into the panel's compare list (default =)
    char             mRamSearchValue[32] = "";// operand entry (decimal, or 0x… hex)
    bool             mRamSearchLow = true;     // scan Low work RAM (0x00200000)
    bool             mRamSearchHigh = true;    // scan High work RAM (0x06000000)
    std::string      mRamSearchStatus;         // last scan result summary

    // "Find what accesses this address" — the access log + its panel's controls.
    AccessLog        mAccessLog;
    uint64_t         mAccessWatchId = 0;        // id of the active logging watchpoint (0 = none)
    int              mAccessSelected = -1;      // row whose call stack the panel shows
    uint32_t         mAccessWatchAddr = 0;      // address it watches (for the header)
    char             mAccessAddr[16] = "";      // address entry (hex)
    int              mAccessKind = 0;           // 0 read+write, 1 read, 2 write
    int              mAccessSizeIdx = 2;        // span combo index; bytes = 1u << idx (1/2/4)

    // SCSP 68000 sound-CPU disassembly panel (over captured Sound RAM).
    uint32_t         mSoundCpuAddr = 0;         // current 68K address (Sound RAM offset)
    char             mSoundCpuAddrBuf[16] = "0";

    // Disc Explorer — an opened disc image + its parsed ISO 9660 file list + panel state.
    DiscImage        mDisc;
    IsoFs            mDiscFs;
    std::string      mDiscStatus;               // open result / error line
    char             mDiscFilter[64] = "";      // substring filter over file paths
    char             mDiscResolve[16] = "";     // sector/FAD to resolve to a file
    bool             mDiscResolveIsFad = false; // interpret the resolve box as a FAD (LBA+150)
    AssemblyPanel            mAssemblyPanel;
    HexEditorPanel           mHexEditor;

    // Frame Memory Compare (Docs/MemoryCompare)
    CompareMarkers           mCompare;
    MemoryComparePanel       mMemoryCompare;
    DiffResult               mCompareDiff;              // what the panel shows (a == null: none); holds both snapshots
    std::string              mCompareStatus;            // why the last mark or compare did not happen
    std::unique_ptr<CsvExport> mCompareExport;          // the CSV export in progress, written a slice per frame
    StringCsvSink            mCompareExportSink;        // ...into this buffer, saved when it is complete
    std::string              mCompareExportName;
    bool                     mOpenCompareExport = false;
    ControllerPanel          mController;
    unsigned int             mInputMask = 0;    // last pad mask sent to the live emulator
    int                      mInputPort = 0;    // ...and the port it went to
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
    bool                     mFocusRegisters = false;   // ditto, for the SH-2 register file
    bool                     mSelectSh2RegTab = false;  // and select its SH-2 tab, not a VDP one
    bool                     mFocusBreakpoints = false; // ditto, for the breakpoint list
    bool                     mCallStackWasShowable = false;  // edge-detect entering paused/loaded
    int                      mCallStackCpu = 0;
    // Height of the frame table above the draggable Frame Detail split, in pixels;
    // persisted. 0 means "not chosen yet" — the panel picks a default on first draw.
    float                    mCallStackSplit = 0.0f;
    // CPU shown by the Registers panel's SH-2 tab. Follows a halt (set alongside
    // mCallStackCpu) so a breakpoint lands you on the registers that stopped, but stays
    // independently switchable so you can read the other CPU without disturbing anything.
    int                      mRegSh2Cpu = 0;
    bool                     mRenameOpen = false;
    uint32_t                 mRenameAddr = 0;
    char                     mRenameBuf[64] = {};

    se_render_opts   mRenderOpts {};
    bool             mbLiveSource = false;    // data comes from a running emulator
    bool             mbPaused = false;        // live emulator held paused (frame control)
    // Run control: the halt presentation, the transient Step Over/Out breakpoint, the hold that
    // keeps a halt on screen across a step's resume->re-halt round trip, and the post-step settle
    // window. Ten members and their invariants, which had no test while they lived here
    // (Docs/CodeReview -- UI-01); see Debug/StepHaltMachine.h for the races they exist to avoid.
    // App keeps the policy: only it resumes, evaluates a condition guard, or opens a panel.
    sfe::StepHaltMachine mStepHalt;
#ifdef SE_ENABLE_LIVE
    // A connection attempt running off the UI thread (see StartLiveOpen). Shared with its worker
    // and never joined: the attempt can sit in a name lookup the application cannot interrupt, so
    // closing the app (or abandoning the attempt) must not wait for it. Whoever finishes second
    // deals with the connection -- the worker closes it if the app has already walked away, the
    // app takes it if the worker got there first.
    struct LiveOpenJob
    {
        std::atomic<bool>  done{false};
        std::mutex         m;
        bool               abandoned = false;   // the app is gone / no longer wants the result
        bool               taken = false;       // the app has claimed the connection
        se_result          result = SE_ERR_IO;
        se_data_source     source = {};
        std::string        endpoint;
        bool               reportFailure = false;   // a user asked for this; say so if it fails

        // Worker: record the outcome. If the app already abandoned the attempt, nobody will adopt
        // the connection, so close it here.
        void Finish(se_result r, const se_data_source& s)
        {
            std::lock_guard<std::mutex> lk(m);
            result = r;
            source = s;
            if (abandoned && r == SE_OK && source.close) { source.close(source.user); }
            done.store(true);
        }
        // App: claim the finished connection (once).
        bool Take(se_result& r, se_data_source& s)
        {
            std::lock_guard<std::mutex> lk(m);
            if (!done.load() || taken) { return false; }
            taken = true;
            r = result;
            s = source;
            return true;
        }
        // App: stop caring. A connection that has already finished and was not claimed is closed.
        void Abandon()
        {
            std::lock_guard<std::mutex> lk(m);
            abandoned = true;
            if (done.load() && !taken && result == SE_OK && source.close) { source.close(source.user); }
            taken = true;
        }
    };
    struct LiveOpenHandle
    {
        std::shared_ptr<LiveOpenJob> job;
        ~LiveOpenHandle() { if (job) { job->Abandon(); } }
    };
    LiveOpenHandle mLiveOpen;
    bool AttachLiveSource(se_data_source& dataSource, const char* endpoint);
    void StartLiveOpen(const char* endpoint, bool reportFailure);
    void PollLiveOpen();
#endif
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
    // Numbered save states. Tracks the emulator's streamed savestate independently of the
    // recorder ring, so Save State works without recording being on.
    SavestateSlots   mStateSlots;
    std::string      mStateStatus;      // last save/load result, shown in the State menu
    // The emulator's own numbered slots, as it reports them (it owns the files; SE cannot
    // find them on disk). mEmuSlotCount 0 = this emulator does not offer them.
    uint8_t          mEmuSlotPresent[SE_LIVE_EMU_SLOTS] = {};
    uint64_t         mEmuSlotMtime[SE_LIVE_EMU_SLOTS] = {};
    uint32_t         mEmuSlotCount = 0;
    // Which of SE's own slots hold a state. Cached rather than stat()ed per frame: the
    // native menu bar rebuilds this view model every frame but only shows it when a menu
    // opens, so polling the filesystem at 60 Hz bought nothing. Refreshed on the events
    // that can change it -- a save, a load, and opening the State menu.
    bool             mSlotOccupied[SavestateSlots::kSlotCount] = {};
    // Slot labels, snapshotted when the State menu opens (each one reads a file header).
    std::string      mSlotLabel[SavestateSlots::kSlotCount];
    void RefreshSlotCache();
    // Notice that the live driver reconnected to a different emulator process and drop
    // everything derived from the run that ended. See the definition.
    void AdoptNewEmulatorInstance();
    uint32_t         mLiveConnGeneration = 0;   // se_live_connection_generation last seen
    void ClearRecordedFrames();   // empty the ring and detach the compare markers
    void DropRecordedHistory();
    // The thing the data panels are editing is about to change underneath them (the transport
    // picked another frame, a slot is being restored). Voids every in-flight edit now and
    // refuses writes for the rest of this frame, because the panels drawn after this point
    // still hold the old context.
    void VoidEditTarget();
    // State loads (rewind "Play from here", a SE slot, an emulator slot) are applied by the
    // emulator later, and it says so in the control block (protocol v19). Edits stay refused
    // until the counters show the load applied *and* a capture newer than that has landed --
    // never because time passed. Silence is reported but does not unlock: the emulator may still
    // apply the load, and a poke before it does is lost or lands on the restored state. What
    // does unlock is a state the app knows: the load resolving, another load resolving it, or
    // the connection being replaced or closed.
    //
    // The baseline must be sampled BEFORE the request is submitted: the emulator and the poll
    // thread can finish it before the submitting call returns, and a baseline taken after would
    // count that completion as already seen and wait for one that never comes.
    struct RestoreBaseline { bool signal = false; uint32_t done = 0, failed = 0; };
    RestoreBaseline SampleRestoreBaseline() const;
    void BeginRestoreWait(const RestoreBaseline& before);
    void ResolveRestoreWait(uint32_t done, uint32_t failed);
    // Emulator pokes: tell the user when one the view shows did not reach the emulator, and feed the
    // server's VDP-write capability to the recorder (see FrameRecorder::SetVdpBusEditsAccepted).
    void ReconcilePokes();
    uint32_t         mPokeDroppedSeen = 0;     // the emulator's dropped-poke count already reported
    uint32_t         mPokeLostSeen = 0;        // the driver's lost-poke count already reported
    uint32_t         mPokeUnconfirmedSeen = 0; // ...and its unconfirmed-poke count
    int              mRecordSeconds = 5;       // ring-buffer window (5..30 s)
    bool             mbRecording = false;      // explicit recording state
    double           mRecordingStartedAt = 0.0;
    se_context*      mScrubContext = nullptr;  // context over the selected past frame
    bool             mbScrubbing = false;      // viewing a recorded (past) frame
    int              mScrubIndex = -1;         // selected recorded-frame index
    int              mScrubShownIndex = -1;    // index currently built into mScrubContext
    uint64_t         mScrubShownFrame = 0;     // ...and the frame number it holds (indexes shift as the ring evicts)
    uint64_t         mScrubTargetFrame = 0;    // a navigation's frame, resolved by number in RefreshScrubContext
    se_context*      mLiveCtx = nullptr;       // the live context, reachable while panels
                                               // render from the scrub context (transport)
    // Rewind (v16): whether the connected server supports "Play from here" (savestate rewind),
    // and the edits made while scrubbed, replayed atop the restored state when rewinding.
    bool             mSeekSupported = false;
    bool             mVdpPokeSupported = false;   // the server applies CRAM / frame-buffer pokes (v23 VDP writer)
    bool             mScrubEdited = false;     // the scrub context shows edits that are no longer staged
    StagedEdits      mStaged;                  // edits made against the scrubbed frame, tagged with its frame number
    void DiscardPendingEdits();                // abandoned frame / ended session: nothing to replay
    int              mCallStackViewKey = -1;   // scrubbed frame the call stack was built for (-1 live)
    // Record an edit made against the scrubbed frame (routed from the recorder's write sink).
    void RecordPendingEdit(int isSound, uint32_t addr, const uint8_t* bytes, size_t len);
    // Build the SE_LIVE_EDIT_* blob from mStaged (for the LST rewind payload).
    // Estimated recorder capacity at the current history length, in MB (per-frame average x
    // the configured frame budget). Shown as the "available" half of the footprint readout.
    double RecorderCapacityMB() const;
    // Recorder callback thunks (static; 'user' is this App).
    static void OnStateBlock(void* user, uint8_t kind, uint32_t frame, uint32_t base,
                             uint32_t fullLen, uint32_t epoch, const uint8_t* payload, uint32_t len);
    static void OnScrubEdit(void* user, int isSound, uint32_t addr, const uint8_t* bytes, size_t len);

    // --- Patch feature: locate memory edits in game files, then apply them back ---
    // The Hex editor's "Find in game files" records where a memory selection lives on disc
    // (via content search of the Data Directory); accepted matches accumulate in mPatchLib.
    // "Apply changes to disc" emits + runs a Python script that writes current memory into the
    // mapped files. The library persists to a project file; unsaved changes warn on close.
    PatchLibrary         mPatchLib;

    bool                 mOpenLocatePopup = false;                 // context-bytes popup pending
    HexEditorPanel::LocateRequest mLocatePending;                  // selection awaiting context
    int                  mLocateBefore = 16, mLocateAfter = 16;    // context byte counts

    // Inputs captured at "Find in game files" time, needed to turn an accepted match into a
    // PatchLocation on the (later) Accept click.
    uint32_t             mLocateAddr = 0, mLocateLen = 0, mLocateCtxBefore = 0;
    std::vector<uint8_t> mLocateExpected;                          // selection bytes (baseline)
    std::string          mLocateLabel;
    // Results of the locate search (shown with Accept/Reject). mLocateRel/mLocateRowAdded are
    // parallel to the flattened (hit, offset) rows and are built once when results arrive.
    bool                 mShowLocateResults = false;
    std::vector<DataSearchHit> mLocateResults;
    std::vector<std::string>   mLocateRel;                         // rel path per result FILE
    std::string          mLocateSummary;
    std::vector<uint8_t> mLocateRowAdded;                          // 1 = row accepted

    bool                 mOpenManageLocations = false;
    bool                 mShowPatchResults = false;
    std::string          mPatchResultText;
    bool                 mClosePrompt = false;                     // unsaved-changes-on-close modal
    bool                 mClosePromptActive = false;               // edge-trigger guard for the modal

    void DrawLocatePopup();
    void BeginLocateSearch(uint32_t addr, uint32_t len, uint32_t before, uint32_t after);
    void DrawLocateResults();
    void AcceptLocateMatch(const std::string& rel, uint64_t selOffset);   // add one match to the library
    void DrawPatchMenu(std::vector<TopBarCommand>& commands);
    void DrawManageLocationsModal();
    void DrawPatchResultsModal();
    void DrawClosePromptModal(IPlatform& platform);
    void ApplyChangesToDisc(IPlatform& platform);
    void DoSaveProject(IPlatform& platform);
    void DoOpenProject(IPlatform& platform);
    std::string RelativeToDataDir(const std::string& absPath) const;
    // Rebuild a disc image from the Data Directory: Track 01 from the modified filesystem (with
    // the original IP.BIN + PVD ids), audio/extra tracks copied from the source disc. Output is
    // BIN/CUE (default) or a data-only ISO; 'launch' then runs it. Result shown in a modal.
    void BuildDisc(IPlatform& platform, bool launch);
    void VerifyEncoder();                           // independent EDC/ECC self-check vs source disc
    void DrawBuildDiscModal(IPlatform& platform);   // the "Build Disc Image" options dialog
    void DrawBuildResultModal();
    bool               mOpenBuildDiscModal = false;
    int                mBuildFormatBinCue = 1;       // 1 = BIN/CUE (default), 0 = ISO
    std::string        mBuildResultText;             // outcome text for the result modal
    bool               mShowBuildResult = false;
#endif

    // Game data directory (a folder of the game's extracted files, or an ISO/disc
    // image) that the texture "Find in game data" search scans. Persisted only in
    // memory for the session. The set-dir modal opens when the user asks for it, or
    // automatically when a search is requested with no directory set yet.
    std::string          mDataDir;
    bool                 mOpenDataDirModal = false;   // request to open the modal
    // Data > Dump SH-2.
    bool                 mOpenDumpSh2Modal = false;   // request to open the options dialog
    bool                 mOpenDumpSh2Progress = false;  // ...and the progress modal, once a job starts
    Sh2DumpOptions       mDumpSh2Options;             // kept between uses
    int                  mDumpSh2Cpu = 0;             // whose registers resolve the generated comments
    bool                 mDumpSh2Available[kSh2DumpRegionCount] = {};   // which regions the source can read
    std::unique_ptr<Sh2DumpJob> mDumpSh2Job;          // set while a dump is being written
    std::string          mDumpSh2FileName;
    bool                 mSearchAfterSetDir = false;  // run pending search once dir set
    std::vector<uint8_t> mPendingNeedle;              // texture bytes to search for
    std::string          mPendingSearchLabel;         // human label for the search
    bool                 mShowSearchResults = false;
    std::vector<DataSearchHit> mSearchResults;        // reaped from the runner, owned here
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

    // The async search worker: its thread, progress, cancellation and the
    // cancel-and-queue-the-next behaviour all live in DataSearchRunner.
    DataSearchRunner     mSearchRunner;

    // Which window a search's results belong to. It travels with the request through the runner
    // (DataSearchRequest::destination), so the running and queued searches can differ without a
    // second flag to keep in step; only the needle waiting for a data directory to be set needs
    // one here, because no request exists for it yet.
    int                        mPendingDestination = kSearchToTextureResults;

    // Per-panel visibility, toggled from the toolbar "Windows" menu. All shown by
    // default; a hidden panel simply isn't drawn (its dock tab disappears until
    // re-enabled). Session-only state — these reset to visible each launch.
    struct Panels
    {
        // Archive Explorer + Search ROM/Files are M6 placeholders, and References is
        // niche — hidden by default (re-enable from the Windows menu).
        bool vramMap = true, archiveExplorer = false, searchRom = false;
        bool vdpOutput = true, worldView = true;
        // Per-layer viewers (LayerPanels.h), tabbed beside VDP Output / 3D View.
        bool layerVdp1 = true, layerNbg0 = true, layerNbg1 = true;
        bool layerNbg2 = true, layerNbg3 = true, layerRbg0 = true;
        bool vdp1Table = true, vdp2Table = true, colorRam = true;
        bool registers = true, commandList = true;
        bool textureViewer = true, paletteViewer = true, references = false;
        bool selectedObject = true, watch = true, assembly = true, hexEditor = true;
        bool controller = true;   // Saturn control pad (drives a live game)
        bool log = true;          // structured event log (tracepoints + system events)
        bool actions = true;      // Tracepoints / execution-actions management table
        bool callStack = true;    // per-CPU call stack (paused-state workspace)
        bool breakpoints = true;  // Visual Studio-style breakpoint list (tabs by Call Stack)
        bool sound = true;        // SCSP voices (live): who's playing + Play/Export
        bool ramSearch = true;    // Cheat-Engine-style live RAM value scanner
        bool accessLog = true;    // "find what accesses this address" (data watchpoint log)
        bool soundCpu = true;     // SCSP 68000 sound-CPU disassembly
        bool discExplorer = true; // disc image ISO 9660 browser (sector -> file)
        bool memoryCompare = false; // Frame Memory Compare: hidden until a comparison is opened
    };
    Panels           mPanels;

    // The single source of truth for every toggleable panel: its settings key, its
    // Windows-menu label, and a pointer to its visibility flag. The Windows menu and
    // settings load/save all iterate this one list, so a new panel is added in
    // exactly one place instead of three parallel enumerations.
    struct PanelInfo { const char* key; const char* label; bool Panels::* flag; const char* category; };
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
    bool             mbLaunchedEmulator = false;  // SE started the current emulator (so a
                                                  // relaunch stops it first, then reconnects)
    bool             mShowTooltips = false;       // hover help on field/register/header labels
    // Rewind capture (Settings). The emulator saves a full state every frame to feed the
    // rewind timeline, which costs real frame rate, so the user owns the switch. Pushed to the
    // emulator over the live protocol (REW, v18) rather than just ignored on this side.
    bool                     mRewindEnabled = true;
                                                  // (Settings > Tooltips; persisted, default off)
    bool             mOpenLaunchSettings = false;    // request to open the Launch Settings modal
    bool             mLaunchSettingsInit = false;    // (re)load edit buffers on modal open
    bool             mOpenRecordingSettings = false;
    bool             mOpenDiffSettings = false;      // request to open the Diff Tool modal
    bool             mLaunchDiffRequested = false;   // Compare Memory chose the external tool; run it this frame
    std::string      mDiffExe;                       // "" = the built-in Memory Compare panel
    std::string      mDiffArgs;                      // template with {a}/{b}; see DiffTool.h
    char             mDiffExeEdit[512] = {};         // edit buffers for the modal
    char             mDiffArgsEdit[256] = {};
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

    // Feature-tour Demo Mode. mDemo sequences the loaded script; the Req flags are set by the
    // toolbar menu / hotkeys and drained in UpdateDemo (which has the platform handle). The
    // overlay is an operator HUD — turn it off before a clean take.
    DemoPlayer       mDemo;
    bool             mDemoOverlay = true;       // show the operator HUD while playing
    bool             mDemoShowNote = false;     // HUD also shows the narration note (teleprompter;
                                                // off by default so a clean take has no captions)
    bool             mDemoReqToggle = false;    // start/stop requested
    bool             mDemoReqNext = false;      // advance one beat
    bool             mDemoReqPrev = false;      // go back one beat
    bool             mDemoReqLoad = false;      // open a .sedemo via the file dialog
    std::string      mDemoScriptName;           // basename of the loaded script (for the HUD)
    std::string      mDemoStatus;               // last load result / error
    // Panel visibility is a persisted preference, so a tour must not leave its own layout
    // behind: snapshot it when the demo starts and put it back however the demo ends.
    Panels           mDemoSavedPanels;
    bool             mDemoPanelsSaved = false;
    // Window to focus, applied at the end of the frame. A panel a beat just revealed has not
    // been submitted when the beat is applied, so SetWindowFocus would not find it yet.
    std::string      mDemoPendingFocus;

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

    // Per-layer viewer tabs. Owns its own textures, export folder and grid toggles;
    // App only hands it the context + render options each frame.
    LayerPanels          mLayerPanels;

    // VDP Output frame texture.
    TextureHandle        mFrameTexture = 0;
    int                  mFrameWidth = 0;
    int                  mFrameHeight = 0;
    std::vector<uint8_t> mFrameBuffer;
    RenderKey            mFrameKey;   // what mFrameBuffer was drawn from

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
