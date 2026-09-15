#include "Win32MenuBar.h"

namespace sfe
{

// ---------------------------------------------------------------------------
// Menu command ids. Fixed items get a stable id; the three variable-length lists
// (emulators, recent ROMs, panels) and the layer toggles occupy contiguous ranges
// so WM_COMMAND can decode "which item" back into a NativeMenuAction + index.
// Ids stay below 0xF000 (the system SC_* range) as Win32 requires.
// ---------------------------------------------------------------------------
namespace
{
enum : UINT
{
    // Session
    ID_LAUNCH = 0xE100,
    ID_LOAD_YABAUSE,
    ID_LOAD_MEDNAFEN,
    ID_LOAD_RAW,
    ID_CONNECT,
    ID_DISCONNECT,
    ID_START_REC,
    ID_STOP_REC,
    ID_REC_SETTINGS,
    ID_CLOSE_SOURCE,
    ID_MANAGE_EMULATORS,
    ID_CHANGE_ROM,
    ID_CLEAR_ROM,
    ID_REVEAL_ROM,
    ID_LAUNCH_SETTINGS,

    // Run
    ID_TOGGLE_PAUSE,
    ID_STEP,

    // Data
    ID_DUMP,
    ID_DATADIR,

    // Patch
    ID_PATCH_APPLY,
    ID_PATCH_MANAGE,
    ID_PATCH_SAVE,
    ID_PATCH_OPEN,
    ID_PATCH_BUILD,

    // Windows
    ID_RESET_LAYOUT,
    ID_SAVE_LAYOUT,

    // Tools
    ID_SCREENSHOT,
    ID_BOOKMARKS,
    ID_COMPARE,

    // Settings
    ID_SETTINGS,
    ID_EMU_PATHS,
    ID_INPUT_SETTINGS,
    ID_TOOLTIPS,

    // Help
    ID_HELP,
    ID_GUIDES,
    ID_UPDATES,
    ID_ABOUT,

    // Demo
    ID_DEMO_TOGGLE,
    ID_DEMO_NEXT,
    ID_DEMO_PREV,
    ID_DEMO_AUTO,
    ID_DEMO_LOAD,
    ID_DEMO_OVERLAY,
    ID_DEMO_NOTE,
};

// Contiguous ranges for the variable / indexed items. Each base is spaced well past the
// fixed ids above and past NM_LAYER_COUNT / the plausible list lengths.
constexpr UINT ID_LAYER_BASE = 0xE200;   // + NativeMenuLayer (NM_LAYER_COUNT entries)
constexpr UINT ID_EMU_BASE   = 0xE800;   // + emulator index
constexpr UINT ID_ROM_BASE   = 0xE900;   // + recent-ROM index
constexpr UINT ID_PANEL_BASE = 0xEA00;   // + PanelList index
// Disabled captions / placeholders (VDP group headings, empty-list "(none)", the Bookmarks /
// Compare stubs). Each gets a unique id from this range, handed out at rebuild time, rather than
// sharing id 0 — so even if one were ever un-grayed, its WM_COMMAND can't be mistaken for id 0
// or collide with a real command. None of these ids is decoded in OnCommand, by design.
constexpr UINT ID_PLACEHOLDER_BASE = 0xEB00;

// The Windows-menu categories, in the same fixed display order as App::DrawWindowsMenu, so the
// native menu groups panels identically and the ToggleWindow index stays the flat PanelList one.
const wchar_t* const kCategoriesW[] = {
    L"Graphics", L"Memory && Data", L"Debugger", L"Audio", L"Files && Input"
};
const char* const kCategories[] = {
    "Graphics", "Memory & Data", "Debugger", "Audio", "Files & Input"
};

// UTF-8 -> UTF-16 for menu labels, doubling any '&' so a filename containing it is shown
// literally instead of being taken as a keyboard mnemonic.
std::wstring Widen(const std::string& s)
{
    std::wstring w;
    if (!s.empty())
    {
        int n = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
        w.resize((size_t)n);
        ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    }
    std::wstring out;
    out.reserve(w.size() + 4);
    for (wchar_t c : w)
    {
        out.push_back(c);
        if (c == L'&') out.push_back(L'&');
    }
    return out;
}
}  // namespace

// ---------------------------------------------------------------------------
// Lifetime / message plumbing
// ---------------------------------------------------------------------------
void Win32MenuBar::Detach()
{
    if (mHwnd) ::SetMenu(mHwnd, nullptr);
    if (mMenu) { ::DestroyMenu(mMenu); mMenu = nullptr; }
    mBuiltKey.clear();
}

void Win32MenuBar::Sync(const NativeMenuState& state)
{
    mState = state;
    mHaveState = true;

    // Only the *structure* (labels + list contents) forces a rebuild; enable/check flags are
    // refreshed lazily on WM_INITMENUPOPUP. Never rebuild while a menu is open — that would
    // destroy the popup under the user — defer it to WM_EXITMENULOOP instead.
    if (!mMenu || state.structureKey != mBuiltKey)
    {
        if (mMenuOpen) mRebuildPending = true;
        else           Rebuild();
    }
}

void Win32MenuBar::Drain(std::vector<NativeMenuAction>& out)
{
    out.insert(out.end(), mQueue.begin(), mQueue.end());
    mQueue.clear();
}

void Win32MenuBar::OnExitMenuLoop()
{
    mMenuOpen = false;
    if (mRebuildPending)
    {
        mRebuildPending = false;
        Rebuild();
    }
}

bool Win32MenuBar::OnCommand(int id)
{
    NativeMenuAction action;

    if (id >= (int)ID_LAYER_BASE && id < (int)ID_LAYER_BASE + NM_LAYER_COUNT)
        action = NativeMenuAction(MenuCommand::LayerToggle, id - (int)ID_LAYER_BASE);
    else if (id >= (int)ID_EMU_BASE && id < (int)ID_EMU_BASE + (int)mState.emulators.size())
        action = NativeMenuAction(MenuCommand::SelectEmulator, id - (int)ID_EMU_BASE);
    else if (id >= (int)ID_ROM_BASE && id < (int)ID_ROM_BASE + (int)mState.recentRoms.size())
        action = NativeMenuAction(MenuCommand::SelectRecentRom, id - (int)ID_ROM_BASE);
    else if (id >= (int)ID_PANEL_BASE && id < (int)ID_PANEL_BASE + (int)mState.panels.size())
        action = NativeMenuAction(MenuCommand::ToggleWindow, id - (int)ID_PANEL_BASE);
    else
    {
        MenuCommand c = MenuCommand::None;
        switch ((UINT)id)
        {
        case ID_LAUNCH:            c = MenuCommand::Launch; break;
        case ID_LOAD_YABAUSE:      c = MenuCommand::LoadYabauseState; break;
        case ID_LOAD_MEDNAFEN:     c = MenuCommand::LoadMednafenState; break;
        case ID_LOAD_RAW:          c = MenuCommand::LoadRawDump; break;
        case ID_CONNECT:           c = MenuCommand::ConnectLive; break;
        case ID_DISCONNECT:        c = MenuCommand::DisconnectLive; break;
        case ID_START_REC:         c = MenuCommand::StartRecording; break;
        case ID_STOP_REC:          c = MenuCommand::StopRecording; break;
        case ID_REC_SETTINGS:      c = MenuCommand::OpenRecordingSettings; break;
        case ID_CLOSE_SOURCE:      c = MenuCommand::CloseSource; break;
        case ID_MANAGE_EMULATORS:  c = MenuCommand::OpenLaunchSettings; break;
        case ID_CHANGE_ROM:        c = MenuCommand::BrowseRom; break;
        case ID_CLEAR_ROM:         c = MenuCommand::ClearRom; break;
        case ID_REVEAL_ROM:        c = MenuCommand::RevealRom; break;
        case ID_LAUNCH_SETTINGS:   c = MenuCommand::OpenLaunchSettings; break;
        case ID_TOGGLE_PAUSE:      c = MenuCommand::TogglePause; break;
        case ID_STEP:              c = MenuCommand::StepFrame; break;
        case ID_DUMP:              c = MenuCommand::DumpMemory; break;
        case ID_DATADIR:           c = MenuCommand::SetDataDirectory; break;
        case ID_PATCH_APPLY:       c = MenuCommand::ApplyChangesToDisc; break;
        case ID_PATCH_MANAGE:      c = MenuCommand::ManageLocations; break;
        case ID_PATCH_SAVE:        c = MenuCommand::SaveProject; break;
        case ID_PATCH_OPEN:        c = MenuCommand::OpenProject; break;
        case ID_PATCH_BUILD:       c = MenuCommand::OpenBuildDiscImage; break;
        case ID_RESET_LAYOUT:      c = MenuCommand::ResetLayout; break;
        case ID_SAVE_LAYOUT:       c = MenuCommand::SaveLayout; break;
        case ID_SCREENSHOT:        c = MenuCommand::TakeScreenshot; break;
        case ID_SETTINGS:          c = MenuCommand::OpenSettings; break;
        case ID_EMU_PATHS:         c = MenuCommand::OpenLaunchSettings; break;
        case ID_INPUT_SETTINGS:    c = MenuCommand::ShowInputSettings; break;
        case ID_TOOLTIPS:          c = MenuCommand::ToggleTooltips; break;
        case ID_HELP:              c = MenuCommand::OpenHelp; break;
        case ID_GUIDES:            c = MenuCommand::OpenGuides; break;
        case ID_UPDATES:           c = MenuCommand::CheckForUpdates; break;
        case ID_ABOUT:             c = MenuCommand::OpenAbout; break;
        case ID_DEMO_TOGGLE:       c = MenuCommand::DemoToggle; break;
        case ID_DEMO_NEXT:         c = MenuCommand::DemoNext; break;
        case ID_DEMO_PREV:         c = MenuCommand::DemoPrev; break;
        case ID_DEMO_AUTO:         c = MenuCommand::DemoToggleAuto; break;
        case ID_DEMO_LOAD:         c = MenuCommand::DemoLoad; break;
        case ID_DEMO_OVERLAY:      c = MenuCommand::DemoToggleOverlay; break;
        case ID_DEMO_NOTE:         c = MenuCommand::DemoToggleNote; break;
        default:                   return false;   // not one of ours (let DefWindowProc have it)
        }
        action = NativeMenuAction(c);
    }

    mQueue.push_back(action);
    return true;
}

// ---------------------------------------------------------------------------
// State refresh — enable/disable, checkmarks, and the two toggling labels
// (Pause/Resume, Play/Stop). Runs on WM_INITMENUPOPUP (and right after a rebuild),
// so a menu always opens showing the current state. Uses MF_BYCOMMAND, which searches
// the whole menu tree, so it does not matter which submenu owns each id.
// ---------------------------------------------------------------------------
namespace
{
void EnableById(HMENU menu, UINT id, bool on)
{
    ::EnableMenuItem(menu, id, MF_BYCOMMAND | (on ? MF_ENABLED : MF_GRAYED));
}
void CheckById(HMENU menu, UINT id, bool on)
{
    ::CheckMenuItem(menu, id, MF_BYCOMMAND | (on ? MF_CHECKED : MF_UNCHECKED));
}
void TextById(HMENU menu, UINT id, const wchar_t* text)
{
    MENUITEMINFOW mii{};
    mii.cbSize = sizeof(mii);
    mii.fMask = MIIM_STRING;
    mii.dwTypeData = const_cast<wchar_t*>(text);
    ::SetMenuItemInfoW(menu, id, FALSE, &mii);
}
}  // namespace

void Win32MenuBar::RefreshState()
{
    if (!mMenu || !mHaveState) return;
    const NativeMenuState& s = mState;

    // Session
    EnableById(mMenu, ID_LAUNCH, s.launchEnabled);
    EnableById(mMenu, ID_LOAD_YABAUSE, s.loadDumpEnabled);
    EnableById(mMenu, ID_LOAD_MEDNAFEN, s.loadDumpEnabled);
    EnableById(mMenu, ID_LOAD_RAW, s.loadDumpEnabled);
    EnableById(mMenu, ID_CONNECT, s.connectEnabled);
    EnableById(mMenu, ID_DISCONNECT, s.disconnectEnabled);
    EnableById(mMenu, ID_START_REC, s.startRecordingEnabled);
    EnableById(mMenu, ID_STOP_REC, s.stopRecordingEnabled);
    EnableById(mMenu, ID_CLOSE_SOURCE, s.closeSourceEnabled);
    EnableById(mMenu, ID_CLEAR_ROM, s.clearRomEnabled);
    EnableById(mMenu, ID_REVEAL_ROM, s.revealRomEnabled);
    for (size_t i = 0; i < s.emulators.size(); ++i)
        CheckById(mMenu, ID_EMU_BASE + (UINT)i, s.emulators[i].selected);
    for (size_t i = 0; i < s.recentRoms.size(); ++i)
        CheckById(mMenu, ID_ROM_BASE + (UINT)i, s.recentRoms[i].current);

    // Run
    TextById(mMenu, ID_TOGGLE_PAUSE, s.paused ? L"Resume\tF6" : L"Pause\tF6");
    EnableById(mMenu, ID_TOGGLE_PAUSE, s.togglePauseEnabled);
    EnableById(mMenu, ID_STEP, s.stepEnabled);

    // Data
    EnableById(mMenu, ID_DUMP, s.dumpEnabled);

    // Layers
    for (int i = 0; i < NM_LAYER_COUNT; ++i)
        CheckById(mMenu, ID_LAYER_BASE + (UINT)i, s.layer[i]);

    // Patch
    EnableById(mMenu, ID_PATCH_APPLY, s.patchApplyEnabled);
    EnableById(mMenu, ID_PATCH_MANAGE, s.patchManageEnabled);
    EnableById(mMenu, ID_PATCH_SAVE, s.patchSaveEnabled);
    EnableById(mMenu, ID_PATCH_BUILD, s.buildDiscEnabled);

    // Windows (panel visibility)
    for (size_t i = 0; i < s.panels.size(); ++i)
        CheckById(mMenu, ID_PANEL_BASE + (UINT)i, s.panels[i].visible);

    // Settings
    CheckById(mMenu, ID_TOOLTIPS, s.tooltips);

    // Demo
    TextById(mMenu, ID_DEMO_TOGGLE, s.demoPlaying ? L"Stop\tF7" : L"Play\tF7");
    EnableById(mMenu, ID_DEMO_TOGGLE, s.demoLoaded);
    EnableById(mMenu, ID_DEMO_NEXT, s.demoPlaying);
    EnableById(mMenu, ID_DEMO_PREV, s.demoPlaying);
    CheckById(mMenu, ID_DEMO_AUTO, s.demoAuto);
    CheckById(mMenu, ID_DEMO_OVERLAY, s.demoOverlay);
    CheckById(mMenu, ID_DEMO_NOTE, s.demoShowNote);
}

void Win32MenuBar::OnInitPopup()
{
    // A menu is opening; make sure its enable/check/labels reflect the newest state.
    RefreshState();
}

// ---------------------------------------------------------------------------
// Build the whole menu tree from the current state, then attach it. Labels carry their
// shortcut hint after a tab ("Launch\tF5"); the shortcuts themselves keep firing through the
// shared ImGui::Shortcut prelude on all platforms, so no Win32 accelerator table is needed.
// ---------------------------------------------------------------------------
namespace
{
void AddItem(HMENU menu, UINT id, const wchar_t* text)
{
    ::AppendMenuW(menu, MF_STRING, id, text);
}
void AddDisabled(HMENU menu, UINT id, const wchar_t* text)  // grayed caption / placeholder (never dispatched)
{
    ::AppendMenuW(menu, MF_STRING | MF_DISABLED | MF_GRAYED, id, text);
}
void AddSep(HMENU menu)
{
    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
}
void AddSub(HMENU parent, HMENU sub, const wchar_t* text)
{
    ::AppendMenuW(parent, MF_POPUP, (UINT_PTR)sub, text);
}
}  // namespace

void Win32MenuBar::Rebuild()
{
    if (!mHwnd) return;

    HMENU bar = ::CreateMenu();
    UINT placeholderId = ID_PLACEHOLDER_BASE;   // handed out to each disabled caption/placeholder

    // ---- Session ----
    {
        HMENU session = ::CreatePopupMenu();
        AddItem(session, ID_LAUNCH, L"Launch\tF5");
        AddSep(session);

        HMENU source = ::CreatePopupMenu();
        HMENU loadDump = ::CreatePopupMenu();
        AddItem(loadDump, ID_LOAD_YABAUSE, L"Yabause Save State...\tCtrl+O");
        AddItem(loadDump, ID_LOAD_MEDNAFEN, L"Mednafen Save State...");
        AddSep(loadDump);
        AddItem(loadDump, ID_LOAD_RAW, L"Raw Memory Dump...");
        AddSub(source, loadDump, L"Load Dump");
        AddItem(source, ID_CONNECT, L"Connect to Emulator (live)");
        AddItem(source, ID_DISCONNECT, L"Disconnect (live)");
        AddSep(source);
        HMENU recording = ::CreatePopupMenu();
        AddItem(recording, ID_START_REC, L"Start Recording");
        AddItem(recording, ID_STOP_REC, L"Stop Recording");
        AddItem(recording, ID_REC_SETTINGS, L"Recording Settings...");
        AddSub(source, recording, L"Recording (live)");
        AddSep(source);
        AddItem(source, ID_CLOSE_SOURCE, L"Close Source");
        AddSub(session, source, L"Source");

        HMENU emulator = ::CreatePopupMenu();
        for (size_t i = 0; i < mState.emulators.size(); ++i)
            AddItem(emulator, ID_EMU_BASE + (UINT)i, Widen(mState.emulators[i].label).c_str());
        if (!mState.emulators.empty()) AddSep(emulator);
        AddItem(emulator, ID_MANAGE_EMULATORS, L"Manage Emulators...");
        AddSub(session, emulator, L"Emulator");

        HMENU game = ::CreatePopupMenu();
        AddItem(game, ID_CHANGE_ROM, L"Change ROM...");
        HMENU recent = ::CreatePopupMenu();
        if (mState.recentRoms.empty())
            AddDisabled(recent, placeholderId++, L"(none)");
        for (size_t i = 0; i < mState.recentRoms.size(); ++i)
            AddItem(recent, ID_ROM_BASE + (UINT)i, Widen(mState.recentRoms[i].label).c_str());
        AddSub(game, recent, L"Recent ROMs");
        AddItem(game, ID_CLEAR_ROM, L"Clear Selection");
        AddItem(game, ID_REVEAL_ROM, L"Reveal in Explorer");
        AddSub(session, game, L"Game / ROM");

        AddSep(session);
        AddItem(session, ID_LAUNCH_SETTINGS, L"Launch Settings...");
        AddSub(bar, session, L"&Session");
    }

    // ---- Layers ----
    {
        HMENU layers = ::CreatePopupMenu();
        AddDisabled(layers, placeholderId++, L"VDP1 (Sprites)");
        AddItem(layers, ID_LAYER_BASE + NM_LAYER_SPRITES, L"Sprites");
        AddItem(layers, ID_LAYER_BASE + NM_LAYER_WIREFRAME, L"Wireframe");
        AddItem(layers, ID_LAYER_BASE + NM_LAYER_BBOX, L"Bounding Boxes");
        AddItem(layers, ID_LAYER_BASE + NM_LAYER_OBJNUM, L"Object Numbers");
        AddSep(layers);
        AddDisabled(layers, placeholderId++, L"VDP2 (Background)");
        AddItem(layers, ID_LAYER_BASE + NM_LAYER_NBG0, L"NBG0 (Scroll A)");
        AddItem(layers, ID_LAYER_BASE + NM_LAYER_NBG1, L"NBG1 (Scroll B)");
        AddItem(layers, ID_LAYER_BASE + NM_LAYER_NBG2, L"NBG2 (Scroll C)");
        AddItem(layers, ID_LAYER_BASE + NM_LAYER_NBG3, L"NBG3 (Scroll D)");
        AddItem(layers, ID_LAYER_BASE + NM_LAYER_RBG0, L"RBG0 (Rotation)");
        AddItem(layers, ID_LAYER_BASE + NM_LAYER_WINDOW, L"Window");
        AddItem(layers, ID_LAYER_BASE + NM_LAYER_COLORCALC, L"Color Calculation");
        AddItem(layers, ID_LAYER_BASE + NM_LAYER_SHADOW, L"Shadow / Highlight");
        AddSub(bar, layers, L"&Layers");
    }

    // ---- Run ----
    {
        HMENU run = ::CreatePopupMenu();
        AddItem(run, ID_TOGGLE_PAUSE, L"Pause\tF6");   // label swapped to Resume in RefreshState
        AddItem(run, ID_STEP, L"Step One Frame\tF10");
        AddSub(bar, run, L"&Run");
    }

    // ---- Data ----
    {
        HMENU data = ::CreatePopupMenu();
        AddItem(data, ID_DUMP, L"Dump Memory\tCtrl+Shift+D");
        AddItem(data, ID_DATADIR, L"Set Data Directory...");
        AddSub(bar, data, L"&Data");
    }

    // ---- Patch (the Win32 build always compiles the live/patch feature) ----
    {
        HMENU patch = ::CreatePopupMenu();
        AddItem(patch, ID_PATCH_APPLY, L"Apply Changes to Disc");
        AddItem(patch, ID_PATCH_MANAGE, L"Manage Locations...");
        AddSep(patch);
        AddItem(patch, ID_PATCH_SAVE, L"Save Project...");
        AddItem(patch, ID_PATCH_OPEN, L"Open Project...");
        AddSep(patch);
        AddItem(patch, ID_PATCH_BUILD, L"Build Disc Image...");
        AddSub(bar, patch, L"&Patch");
    }

    // ---- Windows (panels grouped by category, then layout controls) ----
    {
        HMENU windows = ::CreatePopupMenu();

        // Category display order (portable, unit-tested helper): the known categories first, in the
        // fixed order matching App::DrawWindowsMenu, then any category PanelList introduced that
        // isn't in that list, appended in first-seen order — so a new or renamed category still
        // surfaces its panels instead of silently dropping them.
        const size_t nPreferred = sizeof(kCategories) / sizeof(kCategories[0]);
        const std::vector<std::string> preferred(kCategories, kCategories + nPreferred);
        const std::vector<std::string> catKeys = OrderedMenuCategories(mState.panels, preferred);

        for (size_t c = 0; c < catKeys.size(); ++c)
        {
            HMENU cat = ::CreatePopupMenu();
            for (size_t i = 0; i < mState.panels.size(); ++i)
                if (mState.panels[i].category == catKeys[c])
                    AddItem(cat, ID_PANEL_BASE + (UINT)i, Widen(mState.panels[i].label).c_str());
            // Known categories keep their pre-widened caption (with the doubled '&'); an appended
            // unknown category is widened on the fly. OrderedMenuCategories keeps `preferred` at the
            // front in order, so index c < nPreferred lines up with kCategoriesW[c].
            const std::wstring label = (c < nPreferred) ? kCategoriesW[c] : Widen(catKeys[c]);
            AddSub(windows, cat, label.c_str());
        }
        AddSep(windows);
        AddItem(windows, ID_RESET_LAYOUT, L"Reset Layout");
        AddItem(windows, ID_SAVE_LAYOUT, L"Save Layout");
        AddSub(bar, windows, L"&Windows");
    }

    // ---- Demo ----
    {
        HMENU demo = ::CreatePopupMenu();
        AddItem(demo, ID_DEMO_TOGGLE, L"Play\tF7");   // label swapped to Stop in RefreshState
        AddItem(demo, ID_DEMO_NEXT, L"Next Beat\tF8");
        AddItem(demo, ID_DEMO_PREV, L"Previous Beat\tShift+F8");
        AddItem(demo, ID_DEMO_AUTO, L"Auto-advance (timed)");
        AddSep(demo);
        AddItem(demo, ID_DEMO_LOAD, L"Load Demo Script...");
        AddItem(demo, ID_DEMO_OVERLAY, L"Operator Overlay");
        AddItem(demo, ID_DEMO_NOTE, L"Overlay Shows Narration");
        AddSub(bar, demo, L"De&mo");
    }

    // ---- Tools (Screenshot + the disabled Bookmarks/Compare placeholders) ----
    {
        HMENU tools = ::CreatePopupMenu();
        AddItem(tools, ID_SCREENSHOT, L"Screenshot\tF12");
        AddSep(tools);
        AddDisabled(tools, placeholderId++, L"Bookmarks");
        AddDisabled(tools, placeholderId++, L"Compare");
        AddSub(bar, tools, L"&Tools");
    }

    // ---- Settings ----
    {
        HMENU settings = ::CreatePopupMenu();
        AddItem(settings, ID_SETTINGS, L"Settings...\tCtrl+,");
        AddItem(settings, ID_EMU_PATHS, L"Emulator Paths...");
        AddItem(settings, ID_INPUT_SETTINGS, L"Input Settings...");
        AddSep(settings);
        AddItem(settings, ID_TOOLTIPS, L"Tooltips");
        AddSub(bar, settings, L"Se&ttings");
    }

    // ---- Help ----
    {
        HMENU help = ::CreatePopupMenu();
        AddItem(help, ID_HELP, L"Help");
        AddItem(help, ID_GUIDES, L"Hardware Guides (web)...");
        AddItem(help, ID_UPDATES, L"Check for Updates...");
        AddItem(help, ID_ABOUT, L"About");
        AddSub(bar, help, L"&Help");
    }

    // Swap in the new bar, then free the old one (order matters: the window must no longer
    // reference the old HMENU before it is destroyed).
    HMENU old = mMenu;
    mMenu = bar;
    ::SetMenu(mHwnd, mMenu);
    ::DrawMenuBar(mHwnd);
    if (old) ::DestroyMenu(old);

    mBuiltKey = mState.structureKey;
    RefreshState();
}

}  // namespace sfe
