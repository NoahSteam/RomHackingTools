// TopBar -- state and commands shared by the top toolbar renderer and App.
// The renderer emits commands; App owns all side effects (dialogs, files,
// launching, and live-protocol calls).
#pragma once

#include <cstdint>
#include <string>

namespace sfe
{

enum class SourceType
{
    None,
    Dump,
    Live
};

struct SourceState
{
    SourceType  type = SourceType::None;
    std::string label;
    std::string path;
    std::string endpoint;
};

enum class TopBarCommandType
{
    None,
    LoadYabauseState,
    LoadMednafenState,
    LoadRawDump,
    ConnectLive,
    DisconnectLive,
    CloseSource,
    StartRecording,
    StopRecording,
    OpenRecordingSettings,
    SelectEmulator,
    SelectRecentRom,
    BrowseRom,
    ClearRom,
    RevealRom,
    Launch,
    OpenLaunchSettings,
    TogglePause,
    StepFrame,
    // Save states (live only). 'index' carries the slot.
    SaveState,
    LoadState,
    DumpMemory,
    SetDataDirectory,
    ToggleWindow,
    ShowWindow,
    ResetLayout,
    SaveLayout,
    TakeScreenshot,
    OpenSettings,
    OpenHelp,
    OpenAbout,
    OpenGuides,
    CheckForUpdates,
    // Patch feature (desktop only)
    ApplyChangesToDisc,
    SaveProject,
    OpenProject,
    ManageLocations,
    // Rebuild the game disc image from the Data Directory (desktop only)
    OpenBuildDiscImage
};

struct TopBarCommand
{
    TopBarCommandType type = TopBarCommandType::None;
    int               index = -1;
    std::string       value;

    TopBarCommand() = default;
    explicit TopBarCommand(TopBarCommandType t) : type(t) {}
    TopBarCommand(TopBarCommandType t, int i) : type(t), index(i) {}
    TopBarCommand(TopBarCommandType t, const std::string& v) : type(t), value(v) {}
};

struct TopBarViewModel
{
    SourceType source = SourceType::None;
    bool connected = false;
    bool recording = false;
    bool paused = false;
    bool frameControl = false;
    bool launchValid = false;
    bool operationBusy = false;
    // A savestate has been received and the emulator can restore one, so the save-state
    // slots are usable. False when rewind support is missing from the emulator build.
    bool canSaveState = false;
    std::string launchValidationMessage;
    // Patch feature (desktop only): the number of recorded patch locations and whether a Data
    // Directory is set. Both the ImGui Patch menu and the native menu bar read these through
    // TopBarCommandEnabled so the two front ends share one enablement policy.
    int  patchLocationCount = 0;
    bool hasDataDir = false;
};

// Pure enablement policy matching the specification's state table.
bool TopBarCommandEnabled(TopBarCommandType command, const TopBarViewModel& state);

// A Mednafen launch attaches automatically; other emulators are wired up with the
// manual Connect command. Auto-connect never replaces a dump or an existing live
// source.
bool ShouldAutoConnectAfterLaunch(const std::string& emulatorKey, SourceType source);

}  // namespace sfe
