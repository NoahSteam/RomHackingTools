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
    case TopBarCommandType::LaunchAndConnect:
        return state.launchValid && !state.operationBusy;
    case TopBarCommandType::TogglePause:
        return state.connected && state.frameControl;
    case TopBarCommandType::StepFrame:
        return state.connected && state.frameControl && state.paused;
    case TopBarCommandType::DumpMemory:
        return state.source != SourceType::None && !state.operationBusy;
    case TopBarCommandType::None:
        return false;
    default:
        return true;
    }
}

}  // namespace sfe
