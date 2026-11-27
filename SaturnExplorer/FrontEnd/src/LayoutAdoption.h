// LayoutAdoption -- windows added to the app after a user saved imgui.ini.
//
// ImGui opens a window it has no settings for floating, at its default size. A saved layout only knows the
// windows that existed when it was written, so every window added since needs telling where to dock. The
// ones that belong beside an existing window go through this table; App::AdoptNewPanels handles the rest.
// Kept free of App so a test can build an "old" layout and check the result without a whole application.
#pragma once

#include "imgui.h"
#include "imgui_internal.h"

namespace sfe
{

struct AdoptBeside
{
    const char* title;    // the new window
    const char* beside;   // the window whose dock node it joins (where BuildDefaultLayout puts it)
};

// BuildDefaultLayout docks Structs with Watch and Memory Compare with Memory.
inline const AdoptBeside* AdoptBesideTable(int& count)
{
    static const AdoptBeside kTable[] = {
        { "Memory Compare", "Memory" },
        { "Structs",        "Watch"  },
    };
    count = static_cast<int>(sizeof kTable / sizeof kTable[0]);
    return kTable;
}

// For every window in the table that the saved settings have never seen: dock it into its neighbour's node,
// or 'fallbackNode' when the neighbour has none (it floats, or is missing from the layout too). A window the
// user has since moved has its own settings and is left alone.
inline void AdoptPanelsBeside(ImGuiID fallbackNode)
{
    int n = 0;
    const AdoptBeside* table = AdoptBesideTable(n);
    for (int i = 0; i < n; ++i)
    {
        if (ImGui::FindWindowSettingsByID(ImHashStr(table[i].title)) != nullptr) continue;
        const ImGuiWindowSettings* beside = ImGui::FindWindowSettingsByID(ImHashStr(table[i].beside));
        ImGui::DockBuilderDockWindow(table[i].title, beside && beside->DockId ? beside->DockId : fallbackNode);
    }
}

}  // namespace sfe
