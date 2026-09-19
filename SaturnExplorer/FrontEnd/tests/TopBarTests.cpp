#include <cstring>
#include <iostream>

#include "Launcher.h"
#include "NativeMenu.h"
#include "Settings.h"
#include "TopBar.h"
#include "saturnexplorer/SeTypes.h"

using namespace sfe;

namespace
{
int gFailures = 0;

void Check(bool condition, const char* expression, int line)
{
    if (condition) return;
    std::cerr << "CHECK failed at line " << line << ": " << expression << '\n';
    ++gFailures;
}
}  // namespace

#define CHECK(expression) Check(static_cast<bool>(expression), #expression, __LINE__)

static void TestEnablementMatrix()
{
    TopBarViewModel state;
    CHECK(TopBarCommandEnabled(TopBarCommandType::ConnectLive, state));
    CHECK(!TopBarCommandEnabled(TopBarCommandType::DisconnectLive, state));
    CHECK(!TopBarCommandEnabled(TopBarCommandType::StartRecording, state));
    CHECK(!TopBarCommandEnabled(TopBarCommandType::TogglePause, state));
    CHECK(!TopBarCommandEnabled(TopBarCommandType::DumpMemory, state));

    state.source = SourceType::Dump;
    CHECK(TopBarCommandEnabled(TopBarCommandType::ConnectLive, state));
    CHECK(TopBarCommandEnabled(TopBarCommandType::DumpMemory, state));
    CHECK(!TopBarCommandEnabled(TopBarCommandType::TogglePause, state));

    state.source = SourceType::Live;
    state.connected = true;
    state.frameControl = true;
    CHECK(!TopBarCommandEnabled(TopBarCommandType::ConnectLive, state));
    CHECK(TopBarCommandEnabled(TopBarCommandType::DisconnectLive, state));
    CHECK(TopBarCommandEnabled(TopBarCommandType::StartRecording, state));
    CHECK(TopBarCommandEnabled(TopBarCommandType::TogglePause, state));
    CHECK(!TopBarCommandEnabled(TopBarCommandType::StepFrame, state));

    state.paused = true;
    CHECK(TopBarCommandEnabled(TopBarCommandType::StepFrame, state));
    state.recording = true;
    CHECK(!TopBarCommandEnabled(TopBarCommandType::StartRecording, state));
    CHECK(TopBarCommandEnabled(TopBarCommandType::StopRecording, state));
    CHECK(!TopBarCommandEnabled(TopBarCommandType::LoadRawDump, state));
}

static void TestLaunchModel()
{
    CHECK(BuildLaunchArgs("-a -i \"{rom}\"", "C:\\Games\\Saturn Disc.cue") ==
          "-a -i \"C:\\Games\\Saturn Disc.cue\"");
    CHECK(BuildLaunchArgs("--bios {bios} \"{rom}\"", "game.cue", "") == "--bios \"game.cue\"");
    CHECK(PathBasename("C:\\Games\\disc.cue") == "disc.cue");

    Settings settings;
    settings.Set("emulators", "mednafen", "definitely-missing-mednafen.exe");
    settings.Set("launch", "emulator", "mednafen");
    settings.Set("launch", "rom", "definitely-missing-game.cue");
    Launcher launcher;
    launcher.Load(settings);
    const LaunchValidation validation = launcher.Validate();
    CHECK(!validation.valid);
    CHECK(validation.message.find("executable") != std::string::npos);

    launcher.SetRom("a.cue");
    launcher.SetRom("b.cue");
    launcher.SetRom("a.cue");
    CHECK(launcher.Recent().size() == 2);
    CHECK(launcher.Recent()[0] == "a.cue");

    CHECK(ShouldAutoConnectAfterLaunch("mednafen", SourceType::None));
    CHECK(!ShouldAutoConnectAfterLaunch("yabause", SourceType::None));
    CHECK(!ShouldAutoConnectAfterLaunch("mednafen", SourceType::Dump));
    CHECK(!ShouldAutoConnectAfterLaunch("mednafen", SourceType::Live));
}

// Patch items are enabled through the same policy as everything else (finding #2): Apply / Manage
// / Save need a recorded location, Build Disc Image needs a Data Directory, Open Project is always
// available.
static void TestPatchEnablement()
{
    TopBarViewModel state;   // no locations, no data dir
    CHECK(!TopBarCommandEnabled(TopBarCommandType::ApplyChangesToDisc, state));
    CHECK(!TopBarCommandEnabled(TopBarCommandType::ManageLocations, state));
    CHECK(!TopBarCommandEnabled(TopBarCommandType::SaveProject, state));
    CHECK(!TopBarCommandEnabled(TopBarCommandType::OpenBuildDiscImage, state));
    CHECK(TopBarCommandEnabled(TopBarCommandType::OpenProject, state));

    state.patchLocationCount = 1;
    CHECK(TopBarCommandEnabled(TopBarCommandType::ApplyChangesToDisc, state));
    CHECK(TopBarCommandEnabled(TopBarCommandType::ManageLocations, state));
    CHECK(TopBarCommandEnabled(TopBarCommandType::SaveProject, state));
    CHECK(!TopBarCommandEnabled(TopBarCommandType::OpenBuildDiscImage, state));

    state.hasDataDir = true;
    CHECK(TopBarCommandEnabled(TopBarCommandType::OpenBuildDiscImage, state));
}

// The rebuild fingerprint is stable while only enable/check/visibility flags change, and changes
// the moment a dynamic list (emulators / recent ROMs / panels) does — so the platform rebuilds the
// HMENU exactly when the structure moved, not on every flag flip.
static void TestNativeMenuStructureKey()
{
    NativeMenuState st;
    NativeMenuState::EmulatorItem emu; emu.label = "mednafen"; emu.selected = true;
    st.emulators.push_back(emu);
    NativeMenuState::RecentRomItem rom; rom.label = "game.cue"; rom.current = true;
    st.recentRoms.push_back(rom);
    NativeMenuState::PanelItem panel; panel.category = "Graphics"; panel.label = "VDP Output";
    st.panels.push_back(panel);

    const std::string key = BuildNativeMenuStructureKey(st);
    CHECK(BuildNativeMenuStructureKey(st) == key);                 // deterministic

    // Enable/check/visibility flags are excluded from the key.
    st.emulators[0].selected = false;
    st.recentRoms[0].current = false;
    st.panels[0].visible = true;
    st.launchEnabled = st.paused = st.dumpEnabled = true;
    CHECK(BuildNativeMenuStructureKey(st) == key);

    // Any list content change moves the key.
    NativeMenuState added = st;
    NativeMenuState::RecentRomItem rom2; rom2.label = "other.cue";
    added.recentRoms.push_back(rom2);
    CHECK(BuildNativeMenuStructureKey(added) != key);

    NativeMenuState renamed = st;
    renamed.panels[0].label = "VDP Output (2)";
    CHECK(BuildNativeMenuStructureKey(renamed) != key);

    NativeMenuState recat = st;
    recat.panels[0].category = "Debugger";
    CHECK(BuildNativeMenuStructureKey(recat) != key);
}

// A menu selection maps to exactly the TopBarCommand the toolbar would emit (finding #5). Indexed
// items carry their index through; the view-only toggles are reported as "not a command" so App
// runs them inline.
static void TestNativeMenuActionMapping()
{
    TopBarCommand cmd;

    CHECK(NativeMenuActionToCommand(NativeMenuAction(MenuCommand::Launch), cmd));
    CHECK(cmd.type == TopBarCommandType::Launch);

    CHECK(NativeMenuActionToCommand(NativeMenuAction(MenuCommand::SelectEmulator, 3), cmd));
    CHECK(cmd.type == TopBarCommandType::SelectEmulator && cmd.index == 3);

    CHECK(NativeMenuActionToCommand(NativeMenuAction(MenuCommand::SelectRecentRom, 5), cmd));
    CHECK(cmd.type == TopBarCommandType::SelectRecentRom && cmd.index == 5);

    CHECK(NativeMenuActionToCommand(NativeMenuAction(MenuCommand::ToggleWindow, 12), cmd));
    CHECK(cmd.type == TopBarCommandType::ToggleWindow && cmd.index == 12);

    CHECK(NativeMenuActionToCommand(NativeMenuAction(MenuCommand::OpenBuildDiscImage), cmd));
    CHECK(cmd.type == TopBarCommandType::OpenBuildDiscImage);

    // Input Settings routes to the controller panel by its shared name, not a raw literal.
    CHECK(NativeMenuActionToCommand(NativeMenuAction(MenuCommand::ShowInputSettings), cmd));
    CHECK(cmd.type == TopBarCommandType::ShowWindow && cmd.value == kControllerPanel);

    // View-only toggles are not command-backed.
    CHECK(!NativeMenuActionToCommand(NativeMenuAction(MenuCommand::LayerToggle, NM_LAYER_NBG0), cmd));
    CHECK(!NativeMenuActionToCommand(NativeMenuAction(MenuCommand::ToggleTooltips), cmd));
    CHECK(!NativeMenuActionToCommand(NativeMenuAction(MenuCommand::DemoToggle), cmd));
    CHECK(!NativeMenuActionToCommand(NativeMenuAction(MenuCommand::DemoNext), cmd));
    CHECK(!NativeMenuActionToCommand(NativeMenuAction(MenuCommand::DemoPrev), cmd));
    CHECK(!NativeMenuActionToCommand(NativeMenuAction(MenuCommand::DemoToggleAuto), cmd));
    CHECK(!NativeMenuActionToCommand(NativeMenuAction(MenuCommand::None), cmd));
}

// The NM_LAYER_* index selects the right se_render_opts field to flip, and flips only that one.
static void TestLayerToggle()
{
    se_render_opts opts{};
    ToggleRenderLayer(opts, NM_LAYER_SPRITES);
    CHECK(opts.show_vdp1_sprites == 1);
    ToggleRenderLayer(opts, NM_LAYER_SPRITES);
    CHECK(opts.show_vdp1_sprites == 0);

    ToggleRenderLayer(opts, NM_LAYER_NBG1);
    CHECK(opts.show_layer[SE_LAYER_NBG1] == 1);
    CHECK(opts.show_layer[SE_LAYER_NBG0] == 0);   // neighbours untouched
    CHECK(opts.show_layer[SE_LAYER_NBG2] == 0);

    ToggleRenderLayer(opts, NM_LAYER_RBG0);
    CHECK(opts.show_layer[SE_LAYER_RBG0] == 1);

    ToggleRenderLayer(opts, NM_LAYER_SHADOW);
    CHECK(opts.show_shadow_highlight == 1);

    // Out-of-range index is a no-op (doesn't corrupt any field).
    const se_render_opts before = opts;
    ToggleRenderLayer(opts, NM_LAYER_COUNT);
    ToggleRenderLayer(opts, -1);
    CHECK(std::memcmp(&before, &opts, sizeof(opts)) == 0);
}

// The Windows-menu category order keeps the preferred categories first (even when empty), then
// appends any category a panel uses that isn't preferred, in first-seen order and without repeats —
// so a PanelList category addition/rename can't drop its panels from the menu.
static void TestMenuCategoryOrder()
{
    const std::vector<std::string> preferred = {"Graphics", "Debugger"};
    auto mk = [](const char* cat, const char* label) {
        NativeMenuState::PanelItem p; p.category = cat; p.label = label; return p;
    };
    std::vector<NativeMenuState::PanelItem> panels = {
        mk("Graphics", "A"), mk("Debugger", "B"),
        mk("Audio", "C"), mk("Audio", "D"),   // unknown category, seen twice -> appended once
        mk("Files", "E"),                      // second unknown -> appended after Audio
    };
    const std::vector<std::string> order = OrderedMenuCategories(panels, preferred);
    CHECK(order.size() == 4);
    CHECK(order[0] == "Graphics" && order[1] == "Debugger");   // preferred kept, in order
    CHECK(order[2] == "Audio" && order[3] == "Files");         // unknowns appended in first-seen order

    // Preferred categories survive even when no panel uses them (empty submenus still shown).
    const std::vector<std::string> empty = OrderedMenuCategories({}, preferred);
    CHECK(empty == preferred);
}

static void TestSaveStateEnablement()
{
    // Save states need a live connection *and* a state actually received -- an emulator
    // built without rewind support streams none, so canSaveState stays false and the slots
    // must not look usable.
    TopBarViewModel state;
    CHECK(!TopBarCommandEnabled(TopBarCommandType::SaveState, state));
    CHECK(!TopBarCommandEnabled(TopBarCommandType::LoadState, state));

    state.canSaveState = true;      // a state arrived, but nothing is connected
    CHECK(!TopBarCommandEnabled(TopBarCommandType::SaveState, state));
    CHECK(!TopBarCommandEnabled(TopBarCommandType::LoadState, state));

    state.source = SourceType::Live;
    state.connected = true;
    state.canSaveState = false;     // connected, but no state yet (or rewind unsupported)
    CHECK(!TopBarCommandEnabled(TopBarCommandType::SaveState, state));
    CHECK(!TopBarCommandEnabled(TopBarCommandType::LoadState, state));

    state.canSaveState = true;
    CHECK(TopBarCommandEnabled(TopBarCommandType::SaveState, state));
    CHECK(TopBarCommandEnabled(TopBarCommandType::LoadState, state));

    // Unlike Step, save states do not require the emulator to be paused.
    CHECK(!state.paused);
    CHECK(TopBarCommandEnabled(TopBarCommandType::SaveState, state));
}

int main()
{
    TestEnablementMatrix();
    TestSaveStateEnablement();
    TestLaunchModel();
    TestPatchEnablement();
    TestNativeMenuStructureKey();
    TestNativeMenuActionMapping();
    TestLayerToggle();
    TestMenuCategoryOrder();
    if (gFailures != 0)
    {
        std::cerr << gFailures << " top-bar model check(s) failed\n";
        return 1;
    }
    std::cout << "Top-bar model tests passed\n";
    return 0;
}
