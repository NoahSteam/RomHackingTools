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
    LaunchAndConnect,
    OpenLaunchSettings,
    TogglePause,
    StepFrame,
    DumpMemory,
    SetDataDirectory,
    ToggleWindow,
    ShowWindow,
    ResetLayout,
    SaveLayout,
    TakeScreenshot,
    OpenSettings,
    OpenHelp,
    OpenAbout
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
    std::string launchValidationMessage;
};

// Pure enablement policy matching the specification's state table.
bool TopBarCommandEnabled(TopBarCommandType command, const TopBarViewModel& state);

// A normal Mednafen launch should attach automatically, while the explicit
// Launch-and-Connect action applies to any configured emulator. Neither may
// replace a dump or an existing live source without a manual Connect command.
bool ShouldAutoConnectAfterLaunch(const std::string& emulatorKey,
                                  bool connectRequested, SourceType source);

}  // namespace sfe
