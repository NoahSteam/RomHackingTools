#pragma once

#include "NativeMenu.h"
#include "Settings.h"
#include "Theme.h"

namespace sfe
{
// App calls ApplyPending before constructing windows. Menu requests and settings
// writes use the requested mode even while the current frame still uses the old one.
class ThemePreference
{
public:
    ThemeMode Mode() const { return mRequested; }
    void Load(const Settings& settings)
    {
        mRequested = ParseThemeMode(settings.Get("appearance", "theme", "dark").c_str());
    }
    void Store(Settings& settings) const
    {
        settings.Set("appearance", "theme", ThemeModeName(mRequested));
    }
    bool Request(MenuCommand command)
    {
        if (command != MenuCommand::ThemeLight && command != MenuCommand::ThemeDark) return false;
        mRequested = command == MenuCommand::ThemeLight ? ThemeMode::Light : ThemeMode::Dark;
        return true;
    }
    void ApplyPending() const
    {
        if (GetThemeMode() != mRequested) ApplyThemeColors(ImGui::GetStyle(), mRequested);
    }
private:
    ThemeMode mRequested = ThemeMode::Dark;
};
}
