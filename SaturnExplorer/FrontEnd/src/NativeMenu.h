// NativeMenu -- the portable bridge between App and a platform's *native* menu bar
// (Win32 HMENU today; a Mac NSMenu is a separate future task). The ImGui top toolbar
// stays the default on the SDL2 backends; a platform that offers a real OS menu bar
// builds it from the NativeMenuState App fills each frame and reports selections back
// as NativeMenuAction items. App maps those actions onto the *same* TopBarCommand queue
// the toolbar uses (plus the few view-only toggles the toolbar drives directly), so the
// command-application path is unchanged. Nothing here touches an OS type, so it compiles
// on every platform and the mapping lives in one place (App::DispatchNativeMenuAction).
#pragma once

#include <string>
#include <vector>

// se_render_opts is a plain C struct in the global namespace (saturnexplorer/SeTypes.h). Forward-
// declared here so ToggleRenderLayer can take it by reference without this header pulling in the
// core types — the definition is only needed in NativeMenu.cpp and the unit test.
struct se_render_opts;

namespace sfe
{

// The ImGui window / PanelList label of the controller panel. "Input Settings" (both the ImGui
// Settings menu and the native menu bar) opens this panel by name, so the name lives in one place
// instead of being repeated as a raw string a rename could silently miss.
constexpr char kControllerPanel[] = "Controller";

// Save-state slots offered by the native menu bars. Matches SavestateSlots::kSlotCount, but
// declared here so NativeMenu stays free of the live-only headers.
constexpr int kNativeStateSlots = 10;

// VDP layer/overlay toggles, in the order the ImGui "Layers" menu lists them. Carried as
// the index of a NativeMenuAction whose command is LayerToggle; App maps each to the matching
// se_render_opts field. Kept here (not in the core) because it is purely a menu concern.
enum NativeMenuLayer
{
    NM_LAYER_SPRITES = 0,
    NM_LAYER_WIREFRAME,
    NM_LAYER_BBOX,
    NM_LAYER_OBJNUM,
    NM_LAYER_NBG0,
    NM_LAYER_NBG1,
    NM_LAYER_NBG2,
    NM_LAYER_NBG3,
    NM_LAYER_RBG0,
    NM_LAYER_WINDOW,
    NM_LAYER_COLORCALC,
    NM_LAYER_SHADOW,
    NM_LAYER_COUNT
};

// Every action a native-menu item can request. Most map 1:1 to a TopBarCommandType; the
// tail (LayerToggle .. DemoToggleNote) are the view-only toggles the ImGui toolbar performs
// inline (layer visibility, tooltips, Demo playback) rather than through the command queue.
enum class MenuCommand
{
    None = 0,
    // Session
    Launch,
    LoadYabauseState,
    LoadMednafenState,
    LoadRawDump,
    ConnectLive,
    DisconnectLive,
    StartRecording,
    StopRecording,
    OpenRecordingSettings,
    CloseSource,
    SelectEmulator,       // uses index
    SelectRecentRom,      // uses index
    BrowseRom,
    ClearRom,
    RevealRom,
    OpenLaunchSettings,
    // Run
    TogglePause,
    StepFrame,
    SaveState,            // uses index (slot)
    LoadState,            // uses index (slot)
    // Data
    DumpMemory,
    SetDataDirectory,
    // Patch
    ApplyChangesToDisc,
    ManageLocations,
    SaveProject,
    OpenProject,
    OpenBuildDiscImage,
    // Windows
    ToggleWindow,         // uses index (PanelList index)
    ResetLayout,
    SaveLayout,
    // Tools
    TakeScreenshot,
    // Settings
    OpenSettings,
    ShowInputSettings,
    // Help
    OpenHelp,
    OpenGuides,
    CheckForUpdates,
    OpenAbout,
    // View-only toggles (handled directly by App, not via the command queue)
    LayerToggle,          // uses index (NativeMenuLayer)
    ToggleTooltips,
    DemoToggle,
    DemoNext,
    DemoPrev,
    DemoToggleAuto,
    DemoLoad,
    DemoToggleOverlay,
    DemoToggleNote
};

// One menu selection reported back to App. `index` is meaningful only for the commands
// noted above (SelectEmulator, SelectRecentRom, ToggleWindow, LayerToggle).
struct NativeMenuAction
{
    MenuCommand command = MenuCommand::None;
    int         index = -1;

    NativeMenuAction() = default;
    explicit NativeMenuAction(MenuCommand c, int i = -1) : command(c), index(i) {}
};

// The snapshot App hands the platform each frame. The platform builds the menu structure
// from the dynamic lists and reflects the enable/check flags (MF_GRAYED / MF_CHECKED). It is
// a plain data mirror of the same state the ImGui toolbar reads (BuildTopBarViewModel plus the
// launcher/panel/demo/render state), so the two front ends stay in lock-step.
struct NativeMenuState
{
    // --- Session: per-item enablement (mirrors TopBarCommandEnabled) ---
    bool launchEnabled = false;
    bool loadDumpEnabled = false;
    bool connectEnabled = false;
    bool disconnectEnabled = false;
    bool startRecordingEnabled = false;
    bool stopRecordingEnabled = false;
    bool closeSourceEnabled = false;

    // --- Run ---
    bool paused = false;            // drives the Pause/Resume label + check
    bool togglePauseEnabled = false;
    bool stepEnabled = false;
    // Save states: usable at all, and which of the numbered slots currently hold one.
    bool saveStateEnabled = false;
    bool slotOccupied[kNativeStateSlots] = {};

    // --- Data ---
    bool dumpEnabled = false;

    // --- Layers (indexed by NativeMenuLayer) ---
    bool layer[NM_LAYER_COUNT] = {};

    // --- Settings ---
    bool tooltips = false;

    // --- Patch (the Win32 build always compiles the live/patch feature) ---
    bool patchApplyEnabled = false;
    bool patchManageEnabled = false;
    bool patchSaveEnabled = false;
    bool buildDiscEnabled = false;

    // --- Emulator submenu ---
    struct EmulatorItem { std::string label; bool selected = false; };
    std::vector<EmulatorItem> emulators;

    // --- Game / ROM submenu ---
    struct RecentRomItem { std::string label; bool current = false; };
    std::vector<RecentRomItem> recentRoms;
    bool clearRomEnabled = false;
    bool revealRomEnabled = false;

    // --- Windows submenu: PanelList mirror (index carried by ToggleWindow == PanelList index) ---
    struct PanelItem { std::string category; std::string label; bool visible = false; };
    std::vector<PanelItem> panels;

    // --- Demo submenu ---
    bool demoLoaded = false;
    bool demoPlaying = false;
    bool demoAuto = false;
    bool demoOverlay = false;
    bool demoShowNote = false;

    // A cheap structural fingerprint the platform uses to decide when to rebuild the HMENU
    // (labels/lists changed) vs. only refresh check/enable flags. Filled by App::BuildNativeMenuState.
    std::string structureKey;
};

// --- Portable menu logic (defined in NativeMenu.cpp; unit-tested on Linux) ------------------
// These are the pure pieces of App's native-menu bridge, factored out so they can be exercised
// without a full App. App calls them; runtime behavior is unchanged.

struct TopBarCommand;      // TopBar.h

// The structural fingerprint: labels + dynamic-list contents (emulators, recent ROMs, panels).
// Enable/check/visibility flags are deliberately excluded — only a structural change forces the
// platform to rebuild its menu tree. Stable across unchanged state; changes when a list does.
std::string BuildNativeMenuStructureKey(const NativeMenuState& state);

// Map a menu selection onto the TopBarCommand the toolbar would emit. Returns true and fills
// `out` for command-backed items (including the indexed SelectEmulator / SelectRecentRom /
// ToggleWindow); returns false for the view-only toggles App performs inline (layers, tooltips,
// Demo playback), which carry no command.
bool NativeMenuActionToCommand(const NativeMenuAction& action, TopBarCommand& out);

// Flip one se_render_opts field selected by a NativeMenuLayer index (the LayerToggle mapping).
void ToggleRenderLayer(se_render_opts& opts, int layer);

// The category submenus a Windows-menu build should show, in display order: `preferred` first (in
// that order, kept even if empty), then any category present in `panels` that isn't already listed,
// in first-seen order. Deriving the tail from the panels means a category PanelList adds or renames
// still gets a submenu instead of having its panels silently dropped.
std::vector<std::string> OrderedMenuCategories(const std::vector<NativeMenuState::PanelItem>& panels,
                                               const std::vector<std::string>& preferred);

}  // namespace sfe
