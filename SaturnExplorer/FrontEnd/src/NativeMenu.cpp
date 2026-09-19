// Portable native-menu logic — the pieces of App's native-menu bridge that touch no App state,
// factored here so they can be unit-tested on any platform (see FrontEnd/tests/TopBarTests.cpp).
// App::BuildNativeMenuState / DispatchNativeMenuAction / ToggleMenuLayer call straight into these;
// runtime behavior is identical to the inline versions they replaced.
#include "NativeMenu.h"

#include "TopBar.h"
#include "saturnexplorer/SeTypes.h"

namespace sfe
{

std::string BuildNativeMenuStructureKey(const NativeMenuState& state)
{
    // Labels + list contents only. The \x1f / \x1e control bytes separate items and lists so two
    // different list shapes can never collide into the same key.
    std::string key;
    for (const NativeMenuState::EmulatorItem& e : state.emulators) { key += e.label; key += '\x1f'; }
    key += '\x1e';
    for (const NativeMenuState::RecentRomItem& r : state.recentRoms) { key += r.label; key += '\x1f'; }
    key += '\x1e';
    for (const NativeMenuState::PanelItem& p : state.panels)
    { key += p.category; key += '/'; key += p.label; key += '\x1f'; }
    return key;
}

bool NativeMenuDecodeIndexedId(int id, size_t emulatorCount, size_t recentRomCount,
                               size_t panelCount, NativeMenuAction& out)
{
    struct Group { int base; int count; MenuCommand command; };
    const Group groups[] = {
        { kMenuIdLayerBase,     NM_LAYER_COUNT,            MenuCommand::LayerToggle },
        { kMenuIdEmulatorBase,  (int)emulatorCount,        MenuCommand::SelectEmulator },
        { kMenuIdRecentRomBase, (int)recentRomCount,       MenuCommand::SelectRecentRom },
        { kMenuIdPanelBase,     (int)panelCount,           MenuCommand::ToggleWindow },
        { kMenuIdSaveStateBase, kNativeStateSlots,         MenuCommand::SaveState },
        { kMenuIdLoadStateBase, kNativeStateSlots,         MenuCommand::LoadState },
        { kMenuIdEmuLoadBase,   kNativeStateSlots,         MenuCommand::LoadEmulatorState },
    };
    for (const Group& g : groups)
    {
        if (id >= g.base && id < g.base + g.count)
        {
            out = NativeMenuAction(g.command, id - g.base);
            return true;
        }
    }
    return false;
}

bool NativeMenuActionToCommand(const NativeMenuAction& a, TopBarCommand& out)
{
    switch (a.command)
    {
    case MenuCommand::Launch:            out = TopBarCommand(TopBarCommandType::Launch); return true;
    case MenuCommand::LoadYabauseState:  out = TopBarCommand(TopBarCommandType::LoadYabauseState); return true;
    case MenuCommand::LoadMednafenState: out = TopBarCommand(TopBarCommandType::LoadMednafenState); return true;
    case MenuCommand::LoadRawDump:       out = TopBarCommand(TopBarCommandType::LoadRawDump); return true;
    case MenuCommand::ConnectLive:       out = TopBarCommand(TopBarCommandType::ConnectLive); return true;
    case MenuCommand::DisconnectLive:    out = TopBarCommand(TopBarCommandType::DisconnectLive); return true;
    case MenuCommand::StartRecording:    out = TopBarCommand(TopBarCommandType::StartRecording); return true;
    case MenuCommand::StopRecording:     out = TopBarCommand(TopBarCommandType::StopRecording); return true;
    case MenuCommand::OpenRecordingSettings: out = TopBarCommand(TopBarCommandType::OpenRecordingSettings); return true;
    case MenuCommand::CloseSource:       out = TopBarCommand(TopBarCommandType::CloseSource); return true;
    case MenuCommand::SelectEmulator:    out = TopBarCommand(TopBarCommandType::SelectEmulator, a.index); return true;
    case MenuCommand::SelectRecentRom:   out = TopBarCommand(TopBarCommandType::SelectRecentRom, a.index); return true;
    case MenuCommand::BrowseRom:         out = TopBarCommand(TopBarCommandType::BrowseRom); return true;
    case MenuCommand::ClearRom:          out = TopBarCommand(TopBarCommandType::ClearRom); return true;
    case MenuCommand::RevealRom:         out = TopBarCommand(TopBarCommandType::RevealRom); return true;
    case MenuCommand::OpenLaunchSettings: out = TopBarCommand(TopBarCommandType::OpenLaunchSettings); return true;
    case MenuCommand::TogglePause:       out = TopBarCommand(TopBarCommandType::TogglePause); return true;
    case MenuCommand::StepFrame:         out = TopBarCommand(TopBarCommandType::StepFrame); return true;
    case MenuCommand::SaveState:         out = TopBarCommand(TopBarCommandType::SaveState, a.index); return true;
    case MenuCommand::LoadState:         out = TopBarCommand(TopBarCommandType::LoadState, a.index); return true;
    case MenuCommand::LoadEmulatorState: out = TopBarCommand(TopBarCommandType::LoadEmulatorState, a.index); return true;
    case MenuCommand::DumpMemory:        out = TopBarCommand(TopBarCommandType::DumpMemory); return true;
    case MenuCommand::SetDataDirectory:  out = TopBarCommand(TopBarCommandType::SetDataDirectory); return true;
    case MenuCommand::ApplyChangesToDisc: out = TopBarCommand(TopBarCommandType::ApplyChangesToDisc); return true;
    case MenuCommand::ManageLocations:   out = TopBarCommand(TopBarCommandType::ManageLocations); return true;
    case MenuCommand::SaveProject:       out = TopBarCommand(TopBarCommandType::SaveProject); return true;
    case MenuCommand::OpenProject:       out = TopBarCommand(TopBarCommandType::OpenProject); return true;
    case MenuCommand::OpenBuildDiscImage: out = TopBarCommand(TopBarCommandType::OpenBuildDiscImage); return true;
    case MenuCommand::ToggleWindow:      out = TopBarCommand(TopBarCommandType::ToggleWindow, a.index); return true;
    case MenuCommand::ResetLayout:       out = TopBarCommand(TopBarCommandType::ResetLayout); return true;
    case MenuCommand::SaveLayout:        out = TopBarCommand(TopBarCommandType::SaveLayout); return true;
    case MenuCommand::TakeScreenshot:    out = TopBarCommand(TopBarCommandType::TakeScreenshot); return true;
    case MenuCommand::OpenSettings:      out = TopBarCommand(TopBarCommandType::OpenSettings); return true;
    case MenuCommand::ShowInputSettings: out = TopBarCommand(TopBarCommandType::ShowWindow, kControllerPanel); return true;
    case MenuCommand::OpenHelp:          out = TopBarCommand(TopBarCommandType::OpenHelp); return true;
    case MenuCommand::OpenGuides:        out = TopBarCommand(TopBarCommandType::OpenGuides); return true;
    case MenuCommand::CheckForUpdates:   out = TopBarCommand(TopBarCommandType::CheckForUpdates); return true;
    case MenuCommand::OpenAbout:         out = TopBarCommand(TopBarCommandType::OpenAbout); return true;

    // View-only toggles the front end performs inline (no command queue).
    default:
        return false;
    }
}

std::vector<std::string> OrderedMenuCategories(const std::vector<NativeMenuState::PanelItem>& panels,
                                               const std::vector<std::string>& preferred)
{
    std::vector<std::string> order = preferred;
    for (const NativeMenuState::PanelItem& p : panels)
    {
        bool known = false;
        for (const std::string& c : order) if (c == p.category) { known = true; break; }
        if (!known) order.push_back(p.category);
    }
    return order;
}

void ToggleRenderLayer(se_render_opts& opts, int layer)
{
    auto flip = [](uint8_t& v) { v = v ? 0u : 1u; };
    switch (layer)
    {
    case NM_LAYER_SPRITES:   flip(opts.show_vdp1_sprites); break;
    case NM_LAYER_WIREFRAME: flip(opts.show_wireframe); break;
    case NM_LAYER_BBOX:      flip(opts.show_bounding_boxes); break;
    case NM_LAYER_OBJNUM:    flip(opts.show_object_numbers); break;
    case NM_LAYER_NBG0:      flip(opts.show_layer[SE_LAYER_NBG0]); break;
    case NM_LAYER_NBG1:      flip(opts.show_layer[SE_LAYER_NBG1]); break;
    case NM_LAYER_NBG2:      flip(opts.show_layer[SE_LAYER_NBG2]); break;
    case NM_LAYER_NBG3:      flip(opts.show_layer[SE_LAYER_NBG3]); break;
    case NM_LAYER_RBG0:      flip(opts.show_layer[SE_LAYER_RBG0]); break;
    case NM_LAYER_WINDOW:    flip(opts.show_window); break;
    case NM_LAYER_COLORCALC: flip(opts.show_color_calculation); break;
    case NM_LAYER_SHADOW:    flip(opts.show_shadow_highlight); break;
    default: break;
    }
}

}  // namespace sfe
