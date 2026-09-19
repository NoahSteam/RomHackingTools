#include "TopBar.h"

namespace sfe
{

bool TopBarCommandEnabled(TopBarCommandType command, const TopBarViewModel& state)
{
    switch (command)
    {
    case TopBarCommandType::LoadYabauseState:
    case TopBarCommandType::LoadMednafenState:
    case TopBarCommandType::LoadRawDump:
        return !state.recording && !state.operationBusy;
    case TopBarCommandType::ConnectLive:
        return !state.connected && !state.recording && !state.operationBusy;
    case TopBarCommandType::DisconnectLive:
        return state.connected;
    case TopBarCommandType::CloseSource:
        return state.source != SourceType::None;
    case TopBarCommandType::StartRecording:
        return state.connected && !state.recording;
    case TopBarCommandType::StopRecording:
        return state.recording;
    case TopBarCommandType::Launch:
        return state.launchValid && !state.operationBusy;
    case TopBarCommandType::TogglePause:
        return state.connected && state.frameControl;
    case TopBarCommandType::StepFrame:
        return state.connected && state.frameControl && state.paused;
    // Save needs a state to have arrived (the emulator streams the first one a few seconds
    // in, and never at all if its rewind support is off); Load only needs a slot on disk,
    // which is checked per-slot where the menu is built.
    case TopBarCommandType::SaveState:
        return state.connected && state.canSaveState;
    case TopBarCommandType::LoadState:
        return state.connected && state.canSaveState;
    // The emulator's own slots do not depend on a savestate having reached us -- it loads
    // them itself -- only on its having reported that it has them.
    case TopBarCommandType::LoadEmulatorState:
        return state.connected && state.hasEmulatorStates;
    case TopBarCommandType::DumpMemory:
        return state.source != SourceType::None && !state.operationBusy;
    // Patch feature: Apply / Manage / Save need at least one recorded location; Build Disc Image
    // needs a Data Directory. Open Project is always available (it is how you get locations). This
    // is the single source of truth both front ends consult — no per-front-end drift.
    case TopBarCommandType::ApplyChangesToDisc:
    case TopBarCommandType::ManageLocations:
    case TopBarCommandType::SaveProject:
        return state.patchLocationCount > 0;
    case TopBarCommandType::OpenBuildDiscImage:
        return state.hasDataDir;
    case TopBarCommandType::None:
        return false;
    default:
        return true;
    }
}

bool ShouldAutoConnectAfterLaunch(const std::string& emulatorKey, SourceType source)
{
    return source == SourceType::None && emulatorKey == "mednafen";
}

}  // namespace sfe
