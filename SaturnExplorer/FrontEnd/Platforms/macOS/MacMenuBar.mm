// MacMenuBar.mm -- Cocoa (NSMenu) implementation of the NativeMenu bridge. The macOS counterpart
// of Platforms/Windows/Win32MenuBar.cpp: it renders the same NativeMenuState App produces into a
// standard application menu bar installed as [NSApp mainMenu], and reports selections back as the
// same NativeMenuAction items, which App runs through the shared TopBarCommand path. The command
// ids, menu structure, labels, and enable/check policy deliberately match the Win32 build so the
// two native front ends stay in lock-step; only the OS calls differ (NSMenu vs HMENU).
//
// Built as Objective-C++ (with ARC) and compiled only for the macOS target. All AppKit lives here,
// behind the Cocoa-free MacMenuBar facade, so the shared plain-C++ WebPlatform.cpp can own one.

#import <Cocoa/Cocoa.h>

#include <SDL.h>   // SDL_QUIT: route the menu's Quit through the same close-veto path as the window

#include <string>
#include <vector>

#include "MacMenuBar.h"

namespace sfe
{

// ---------------------------------------------------------------------------
// Menu command ids -- identical scheme to Win32MenuBar: fixed items get a stable id; the three
// variable-length lists (emulators, recent ROMs, panels) and the layer toggles occupy contiguous
// ranges so a selection's NSMenuItem.tag decodes back into a NativeMenuAction + index. (There is no
// Win32 SC_* range to avoid on macOS, but keeping the same numbers keeps the two files aligned.)
// ---------------------------------------------------------------------------
namespace
{
enum : int
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

// Contiguous ranges for the variable / indexed items (same bases as Win32MenuBar).
// The indexed-group bases live in NativeMenu.h, shared with Win32MenuBar and the decoder.
constexpr int ID_LAYER_BASE     = kMenuIdLayerBase;
constexpr int ID_EMU_BASE       = kMenuIdEmulatorBase;
constexpr int ID_ROM_BASE       = kMenuIdRecentRomBase;
constexpr int ID_PANEL_BASE     = kMenuIdPanelBase;
constexpr int ID_SAVESTATE_BASE = kMenuIdSaveStateBase;
constexpr int ID_LOADSTATE_BASE = kMenuIdLoadStateBase;
constexpr int ID_EMULOAD_BASE   = kMenuIdEmuLoadBase;

// The Windows-menu categories, in the same fixed display order as App::DrawWindowsMenu, so the
// native menu groups panels identically and the ToggleWindow index stays the flat PanelList one.
const char* const kCategories[] = {
    "Graphics", "Memory & Data", "Debugger", "Audio", "Files & Input"
};

NSString* Str(const std::string& s)
{
    NSString* out = [NSString stringWithUTF8String:s.c_str()];
    return out ? out : @"";   // stringWithUTF8String returns nil on invalid UTF-8; never hand nil to AppKit
}
}  // namespace

}  // namespace sfe

// ---------------------------------------------------------------------------
// Objective-C glue: one target object receives every item's action and the menu-open callbacks,
// forwarding into the C++ Impl. Declared before Impl's out-of-line methods use it; its methods are
// implemented after Impl so they can call into it.
// ---------------------------------------------------------------------------
@interface SEMenuTarget : NSObject <NSMenuDelegate>
{
@public
    sfe::MacMenuBarImpl* owner;   // not owned; the Impl outlives this object
}
- (void)onMenuItem:(NSMenuItem*)sender;
- (void)onQuit:(id)sender;
@end

// ---------------------------------------------------------------------------
// C++ Impl -- holds the state mirror, the queue, and the live NSMenu (all AppKit refs are ARC
// __strong members of this ObjC++ struct).
// ---------------------------------------------------------------------------
namespace sfe
{

struct MacMenuBarImpl
{
    NativeMenuState mState;
    bool            mHaveState = false;
    int             mOpenDepth = 0;          // >0 while any menu is tracking (defer rebuilds)
    bool            mRebuildPending = false;
    std::string     mBuiltKey;               // structureKey the current menu was built from
    std::vector<NativeMenuAction> mQueue;

    SEMenuTarget*   mTarget = nil;
    NSMenu*         mSavedMainMenu = nil;    // whatever SDL installed, restored on Detach
    NSMenu*         mMainMenu = nil;
    // tag -> item, so RefreshState can flip enable/check/labels without walking the tree. Items
    // that are never refreshed (always-enabled commands, placeholders) simply aren't looked up.
    NSMutableDictionary<NSNumber*, NSMenuItem*>* mItems = nil;

    MacMenuBarImpl()
    {
        mTarget = [[SEMenuTarget alloc] init];
        mTarget->owner = this;
    }

    void Sync(const NativeMenuState& state)
    {
        // AppKit is main-thread only; App drives this from the (main-thread) frame loop.
        // NSCAssert, not NSAssert: this is a C++ member function, and NSAssert expands to
        // reference the self/_cmd that only exist inside an Objective-C method body.
        NSCAssert([NSThread isMainThread], @"MacMenuBar::Sync must run on the main thread (AppKit)");
        mState = state;
        mHaveState = true;
        // Only the *structure* (labels + list contents) forces a rebuild; enable/check flags are
        // refreshed lazily in menuNeedsUpdate:. Never rebuild while a menu is open -- that would
        // collapse the popup under the user -- defer it to when tracking ends.
        if (!mMainMenu || state.structureKey != mBuiltKey)
        {
            if (mOpenDepth > 0) mRebuildPending = true;
            else                Rebuild();
        }
    }

    void Drain(std::vector<NativeMenuAction>& out)
    {
        out.insert(out.end(), mQueue.begin(), mQueue.end());
        mQueue.clear();
    }

    void Detach()
    {
        // Idempotent: clearing mSavedMainMenu makes a second call (e.g. the destructor after
        // WebPlatform::Shutdown already detached) a no-op, so we never touch a torn-down NSApp.
        if (mSavedMainMenu) { [NSApp setMainMenu:mSavedMainMenu]; mSavedMainMenu = nil; }
        mMainMenu = nil;
        mItems = nil;
        mBuiltKey.clear();
    }

    void MenuOpened() { ++mOpenDepth; }
    void MenuClosed()
    {
        if (mOpenDepth > 0) --mOpenDepth;
        if (mOpenDepth == 0 && mRebuildPending)
        {
            mRebuildPending = false;
            Rebuild();
        }
    }

    // Decode a selected item's tag into a NativeMenuAction and queue it (mirrors Win32MenuBar::
    // OnCommand). Returns silently for an unknown tag.
    void EnqueueTag(int id)
    {
        NativeMenuAction action;
        if (!NativeMenuDecodeIndexedId(id, mState.emulators.size(), mState.recentRoms.size(),
                                       mState.panels.size(), action))
        {
            MenuCommand c = MenuCommand::None;
            switch (id)
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
            default:                   return;   // not one of ours
            }
            action = NativeMenuAction(c);
        }
        mQueue.push_back(action);
    }

    // -------- building --------
    void AddItem(NSMenu* menu, int tag, NSString* title)
    {
        NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:title
                                                      action:@selector(onMenuItem:)
                                               keyEquivalent:@""];
        item.target = mTarget;
        item.tag = tag;
        [menu addItem:item];
        mItems[@(tag)] = item;   // last writer wins; the few duplicated tags (About/Settings in
                                 // both the app menu and Help/Settings) are never refreshed, so OK
    }
    void AddDisabled(NSMenu* menu, NSString* title)   // grayed caption / placeholder (never dispatched)
    {
        NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:title action:nil keyEquivalent:@""];
        item.enabled = NO;
        [menu addItem:item];
    }
    void AddSep(NSMenu* menu) { [menu addItem:[NSMenuItem separatorItem]]; }
    NSMenu* AddSub(NSMenu* parent, NSString* title)
    {
        NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:title action:nil keyEquivalent:@""];
        NSMenu* sub = [[NSMenu alloc] initWithTitle:title];
        sub.autoenablesItems = NO;      // we drive enable/disable ourselves from NativeMenuState
        sub.delegate = mTarget;         // menuNeedsUpdate: refreshes flags before it shows
        [item setSubmenu:sub];
        [parent addItem:item];
        return sub;
    }

    void Rebuild()
    {
        if (mSavedMainMenu == nil) mSavedMainMenu = [NSApp mainMenu];

        NSString* appName = [[NSProcessInfo processInfo] processName];
        NSMenu* bar = [[NSMenu alloc] initWithTitle:@""];
        bar.autoenablesItems = NO;
        bar.delegate = mTarget;
        mItems = [NSMutableDictionary dictionary];

        // ---- Application menu (required first item; macOS shows the app name here) ----
        {
            NSMenu* app = AddSub(bar, @"App");   // title ignored by AppKit for the app menu
            AddItem(app, ID_ABOUT, [@"About " stringByAppendingString:appName]);
            AddSep(app);
            // Standard ⌘, for Settings. Built manually (not via AddItem) for two reasons: it needs a
            // key-equivalent, and it shares tag ID_SETTINGS with the top-level Settings menu item, so
            // it must stay out of the tag->item map (mItems, last-writer-wins) — neither is refreshed,
            // and both route through onMenuItem:. ⌘, is distinct from the ImGui Ctrl+, shortcut, so
            // there is no double-dispatch.
            NSMenuItem* prefs = [[NSMenuItem alloc] initWithTitle:@"Settings…"
                                                           action:@selector(onMenuItem:) keyEquivalent:@","];
            prefs.target = mTarget;
            prefs.tag = ID_SETTINGS;
            [app addItem:prefs];
            AddSep(app);
            // Standard app-menu items handled by AppKit itself (not our command path).
            NSMenuItem* hide = [[NSMenuItem alloc] initWithTitle:[@"Hide " stringByAppendingString:appName]
                                                          action:@selector(hide:) keyEquivalent:@"h"];
            [app addItem:hide];
            NSMenuItem* hideOthers = [[NSMenuItem alloc] initWithTitle:@"Hide Others"
                                                                action:@selector(hideOtherApplications:)
                                                         keyEquivalent:@"h"];
            hideOthers.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagOption;
            [app addItem:hideOthers];
            NSMenuItem* showAll = [[NSMenuItem alloc] initWithTitle:@"Show All"
                                                             action:@selector(unhideAllApplications:)
                                                      keyEquivalent:@""];
            [app addItem:showAll];
            AddSep(app);
            // Quit routes through SDL_QUIT (not [NSApp terminate:]) so the app's unsaved-changes
            // window-close veto still runs -- same path as clicking the window's red button.
            NSMenuItem* quit = [[NSMenuItem alloc] initWithTitle:[@"Quit " stringByAppendingString:appName]
                                                          action:@selector(onQuit:) keyEquivalent:@"q"];
            quit.target = mTarget;
            [app addItem:quit];
        }

        // ---- Session ----
        {
            NSMenu* session = AddSub(bar, @"Session");
            AddItem(session, ID_LAUNCH, @"Launch\tF5");
            AddSep(session);

            NSMenu* source = AddSub(session, @"Source");
            NSMenu* loadDump = AddSub(source, @"Load Dump");
            AddItem(loadDump, ID_LOAD_YABAUSE, @"Yabause Save State…\tCtrl+O");
            AddItem(loadDump, ID_LOAD_MEDNAFEN, @"Mednafen Save State…");
            AddSep(loadDump);
            AddItem(loadDump, ID_LOAD_RAW, @"Raw Memory Dump…");
            AddItem(source, ID_CONNECT, @"Connect to Emulator (live)");
            AddItem(source, ID_DISCONNECT, @"Disconnect (live)");
            AddSep(source);
            NSMenu* recording = AddSub(source, @"Recording (live)");
            AddItem(recording, ID_START_REC, @"Start Recording");
            AddItem(recording, ID_STOP_REC, @"Stop Recording");
            AddItem(recording, ID_REC_SETTINGS, @"Recording Settings…");
            AddSep(source);
            AddItem(source, ID_CLOSE_SOURCE, @"Close Source");

            NSMenu* emulator = AddSub(session, @"Emulator");
            for (size_t i = 0; i < mState.emulators.size(); ++i)
                AddItem(emulator, ID_EMU_BASE + (int)i, Str(mState.emulators[i].label));
            if (!mState.emulators.empty()) AddSep(emulator);
            AddItem(emulator, ID_MANAGE_EMULATORS, @"Manage Emulators…");

            NSMenu* game = AddSub(session, @"Game / ROM");
            AddItem(game, ID_CHANGE_ROM, @"Change ROM…");
            NSMenu* recent = AddSub(game, @"Recent ROMs");
            if (mState.recentRoms.empty())
                AddDisabled(recent, @"(none)");
            for (size_t i = 0; i < mState.recentRoms.size(); ++i)
                AddItem(recent, ID_ROM_BASE + (int)i, Str(mState.recentRoms[i].label));
            AddItem(game, ID_CLEAR_ROM, @"Clear Selection");
            AddItem(game, ID_REVEAL_ROM, @"Reveal in Finder");

            AddSep(session);
            AddItem(session, ID_LAUNCH_SETTINGS, @"Launch Settings…");
        }

        // ---- Layers ----
        {
            NSMenu* layers = AddSub(bar, @"Layers");
            AddDisabled(layers, @"VDP1 (Sprites)");
            AddItem(layers, ID_LAYER_BASE + NM_LAYER_SPRITES, @"Sprites");
            AddItem(layers, ID_LAYER_BASE + NM_LAYER_WIREFRAME, @"Wireframe");
            AddItem(layers, ID_LAYER_BASE + NM_LAYER_BBOX, @"Bounding Boxes");
            AddItem(layers, ID_LAYER_BASE + NM_LAYER_OBJNUM, @"Object Numbers");
            AddSep(layers);
            AddDisabled(layers, @"VDP2 (Background)");
            AddItem(layers, ID_LAYER_BASE + NM_LAYER_NBG0, @"NBG0 (Scroll A)");
            AddItem(layers, ID_LAYER_BASE + NM_LAYER_NBG1, @"NBG1 (Scroll B)");
            AddItem(layers, ID_LAYER_BASE + NM_LAYER_NBG2, @"NBG2 (Scroll C)");
            AddItem(layers, ID_LAYER_BASE + NM_LAYER_NBG3, @"NBG3 (Scroll D)");
            AddItem(layers, ID_LAYER_BASE + NM_LAYER_RBG0, @"RBG0 (Rotation)");
            AddItem(layers, ID_LAYER_BASE + NM_LAYER_WINDOW, @"Window");
            AddItem(layers, ID_LAYER_BASE + NM_LAYER_COLORCALC, @"Color Calculation");
            AddItem(layers, ID_LAYER_BASE + NM_LAYER_SHADOW, @"Shadow / Highlight");
        }

        // ---- Run ----
        {
            NSMenu* run = AddSub(bar, @"Run");
            AddItem(run, ID_TOGGLE_PAUSE, @"Pause\tF6");   // label swapped to Resume in RefreshState
            AddItem(run, ID_STEP, @"Step One Frame\tF10");
            [run addItem:[NSMenuItem separatorItem]];
            NSMenu* save = AddSub(run, @"Save State");
            NSMenu* load = AddSub(run, @"Load State");
            NSMenu* emu = AddSub(run, @"Load Emulator State");
            for (int i = 0; i < kNativeStateSlots; ++i)
            {
                AddItem(save, ID_SAVESTATE_BASE + i, [NSString stringWithFormat:@"Slot %d", i]);
                AddItem(load, ID_LOADSTATE_BASE + i, [NSString stringWithFormat:@"Slot %d", i]);
                AddItem(emu, ID_EMULOAD_BASE + i, [NSString stringWithFormat:@"Slot %d", i]);
            }
        }

        // ---- Data ----
        {
            NSMenu* data = AddSub(bar, @"Data");
            AddItem(data, ID_DUMP, @"Dump Memory\tCtrl+Shift+D");
            AddItem(data, ID_DATADIR, @"Set Data Directory…");
        }

        // ---- Patch ----
        {
            NSMenu* patch = AddSub(bar, @"Patch");
            AddItem(patch, ID_PATCH_APPLY, @"Apply Changes to Disc");
            AddItem(patch, ID_PATCH_MANAGE, @"Manage Locations…");
            AddSep(patch);
            AddItem(patch, ID_PATCH_SAVE, @"Save Project…");
            AddItem(patch, ID_PATCH_OPEN, @"Open Project…");
            AddSep(patch);
            AddItem(patch, ID_PATCH_BUILD, @"Build Disc Image…");
        }

        // ---- Windows (panels grouped by category, then layout controls) ----
        {
            NSMenu* windows = AddSub(bar, @"Windows");

            // Category order via the portable, unit-tested helper: the known categories first (in
            // the App::DrawWindowsMenu order), then any category PanelList introduced that isn't in
            // that list, appended in first-seen order -- so a new/renamed category still surfaces.
            const size_t nPreferred = sizeof(kCategories) / sizeof(kCategories[0]);
            const std::vector<std::string> preferred(kCategories, kCategories + nPreferred);
            const std::vector<std::string> catKeys = OrderedMenuCategories(mState.panels, preferred);

            for (const std::string& cat : catKeys)
            {
                NSMenu* catMenu = AddSub(windows, Str(cat));
                for (size_t i = 0; i < mState.panels.size(); ++i)
                    if (mState.panels[i].category == cat)
                        AddItem(catMenu, ID_PANEL_BASE + (int)i, Str(mState.panels[i].label));
            }
            AddSep(windows);
            AddItem(windows, ID_RESET_LAYOUT, @"Reset Layout");
            AddItem(windows, ID_SAVE_LAYOUT, @"Save Layout");
        }

        // ---- Demo ----
        {
            NSMenu* demo = AddSub(bar, @"Demo");
            AddItem(demo, ID_DEMO_TOGGLE, @"Play\tF7");   // label swapped to Stop in RefreshState
            AddItem(demo, ID_DEMO_NEXT, @"Next Beat\tF8");
            AddItem(demo, ID_DEMO_PREV, @"Previous Beat\tShift+F8");
            AddItem(demo, ID_DEMO_AUTO, @"Auto-advance (timed)");
            AddSep(demo);
            AddItem(demo, ID_DEMO_LOAD, @"Load Demo Script…");
            AddItem(demo, ID_DEMO_OVERLAY, @"Operator Overlay");
            AddItem(demo, ID_DEMO_NOTE, @"Overlay Shows Narration");
        }

        // ---- Tools ----
        {
            NSMenu* tools = AddSub(bar, @"Tools");
            AddItem(tools, ID_SCREENSHOT, @"Screenshot\tF12");
            AddSep(tools);
            AddDisabled(tools, @"Bookmarks");
            AddDisabled(tools, @"Compare");
        }

        // ---- Settings ----
        {
            NSMenu* settings = AddSub(bar, @"Settings");
            AddItem(settings, ID_SETTINGS, @"Settings…\tCtrl+,");
            AddItem(settings, ID_EMU_PATHS, @"Emulator Paths…");
            AddItem(settings, ID_INPUT_SETTINGS, @"Input Settings…");
            AddSep(settings);
            AddItem(settings, ID_TOOLTIPS, @"Tooltips");
        }

        // ---- Help ----
        {
            NSMenu* help = AddSub(bar, @"Help");
            AddItem(help, ID_HELP, @"Help");
            AddItem(help, ID_GUIDES, @"Hardware Guides (web)…");
            AddItem(help, ID_UPDATES, @"Check for Updates…");
            AddItem(help, ID_ABOUT, @"About");
        }

        [NSApp setMainMenu:bar];
        mMainMenu = bar;
        mBuiltKey = mState.structureKey;
        RefreshState();
    }

    // -------- enable / check / toggling-label refresh (mirrors Win32MenuBar::RefreshState) --------
    void EnableTag(int tag, bool on) { mItems[@(tag)].enabled = on ? YES : NO; }
    void CheckTag(int tag, bool on)  { mItems[@(tag)].state = on ? NSControlStateValueOn : NSControlStateValueOff; }
    void TitleTag(int tag, NSString* t) { mItems[@(tag)].title = t; }

    void RefreshState()
    {
        if (!mMainMenu || !mHaveState) return;
        const NativeMenuState& s = mState;

        // Session
        EnableTag(ID_LAUNCH, s.launchEnabled);
        EnableTag(ID_LOAD_YABAUSE, s.loadDumpEnabled);
        EnableTag(ID_LOAD_MEDNAFEN, s.loadDumpEnabled);
        EnableTag(ID_LOAD_RAW, s.loadDumpEnabled);
        EnableTag(ID_CONNECT, s.connectEnabled);
        EnableTag(ID_DISCONNECT, s.disconnectEnabled);
        EnableTag(ID_START_REC, s.startRecordingEnabled);
        EnableTag(ID_STOP_REC, s.stopRecordingEnabled);
        EnableTag(ID_CLOSE_SOURCE, s.closeSourceEnabled);
        EnableTag(ID_CLEAR_ROM, s.clearRomEnabled);
        EnableTag(ID_REVEAL_ROM, s.revealRomEnabled);
        for (size_t i = 0; i < s.emulators.size(); ++i)
            CheckTag(ID_EMU_BASE + (int)i, s.emulators[i].selected);
        for (size_t i = 0; i < s.recentRoms.size(); ++i)
            CheckTag(ID_ROM_BASE + (int)i, s.recentRoms[i].current);

        // Run
        TitleTag(ID_TOGGLE_PAUSE, s.paused ? @"Resume\tF6" : @"Pause\tF6");
        EnableTag(ID_TOGGLE_PAUSE, s.togglePauseEnabled);
        EnableTag(ID_STEP, s.stepEnabled);
        // Save needs a state in hand; Load additionally needs that slot to hold one.
        for (int i = 0; i < kNativeStateSlots; ++i)
        {
            EnableTag(ID_SAVESTATE_BASE + i, s.saveStateEnabled);
            EnableTag(ID_LOADSTATE_BASE + i, s.saveStateEnabled && s.slotOccupied[i]);
            EnableTag(ID_EMULOAD_BASE + i, s.emuSlotsOffered && s.emuSlotOccupied[i]);
        }

        // Data
        EnableTag(ID_DUMP, s.dumpEnabled);

        // Layers
        for (int i = 0; i < NM_LAYER_COUNT; ++i)
            CheckTag(ID_LAYER_BASE + i, s.layer[i]);

        // Patch
        EnableTag(ID_PATCH_APPLY, s.patchApplyEnabled);
        EnableTag(ID_PATCH_MANAGE, s.patchManageEnabled);
        EnableTag(ID_PATCH_SAVE, s.patchSaveEnabled);
        EnableTag(ID_PATCH_BUILD, s.buildDiscEnabled);

        // Windows (panel visibility)
        for (size_t i = 0; i < s.panels.size(); ++i)
            CheckTag(ID_PANEL_BASE + (int)i, s.panels[i].visible);

        // Settings
        CheckTag(ID_TOOLTIPS, s.tooltips);

        // Demo
        TitleTag(ID_DEMO_TOGGLE, s.demoPlaying ? @"Stop\tF7" : @"Play\tF7");
        EnableTag(ID_DEMO_TOGGLE, s.demoLoaded);
        EnableTag(ID_DEMO_NEXT, s.demoPlaying);
        EnableTag(ID_DEMO_PREV, s.demoPlaying);
        CheckTag(ID_DEMO_AUTO, s.demoAuto);
        CheckTag(ID_DEMO_OVERLAY, s.demoOverlay);
        CheckTag(ID_DEMO_NOTE, s.demoShowNote);
    }
};

// -------- MacMenuBar facade forwarding --------
MacMenuBar::MacMenuBar() : mImpl(new MacMenuBarImpl()) {}
MacMenuBar::~MacMenuBar() { if (mImpl) mImpl->Detach(); delete mImpl; }
void MacMenuBar::Sync(const NativeMenuState& state) { mImpl->Sync(state); }
void MacMenuBar::Drain(std::vector<NativeMenuAction>& out) { mImpl->Drain(out); }
void MacMenuBar::Detach() { mImpl->Detach(); }

}  // namespace sfe

// ---------------------------------------------------------------------------
@implementation SEMenuTarget
- (void)onMenuItem:(NSMenuItem*)sender
{
    if (owner) owner->EnqueueTag((int)sender.tag);
}
- (void)onQuit:(id)sender
{
    (void)sender;
    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_QUIT;
    SDL_PushEvent(&e);
}
// NSMenuDelegate: refresh enable/check/labels the instant a menu is about to show (the Cocoa
// counterpart of WM_INITMENUPOPUP), and track tracking depth so a structure rebuild is deferred
// until every menu has closed.
- (void)menuNeedsUpdate:(NSMenu*)menu
{
    (void)menu;
    if (owner) owner->RefreshState();
}
- (void)menuWillOpen:(NSMenu*)menu
{
    (void)menu;
    if (owner) owner->MenuOpened();
}
- (void)menuDidClose:(NSMenu*)menu
{
    (void)menu;
    if (owner) owner->MenuClosed();
}
@end
