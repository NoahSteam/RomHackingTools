// Settings — small persistent app config stored as an INI in the per-user
// config directory, so preferences survive across runs independent of the
// working directory. Layout (imgui.ini) is relocated next to it by App.
//
//   Windows : %APPDATA%\SaturnExplorer\settings.ini
//   else    : $XDG_CONFIG_HOME/SaturnExplorer/settings.ini  (or ~/.config/...)
//
// The SAME file is written by the installer (Integration/install.py) to record
// the built emulator executables, so the viewer's "Launch" menu can start them
// directly (see Launcher.h). Format is a minimal INI:
//
//   [emulators]
//   mednafen = C:\...\mednafen.exe
//   yabause  = C:\...\yabause-qt.exe
//   [launch]                 ; Launch Session state (Launcher)
//   emulator = mednafen
//   rom      = C:\games\sakura1.cue
//   recent0  = C:\games\sakura1.cue
//   setdatadir = 1
//   [launch.mednafen]        ; per-emulator overrides
//   args     = "{rom}"
//   [panels]
//   references = 0
//   [data]
//   dir = C:\games\sakura
#pragma once

#include <map>
#include <string>

namespace sfe
{

class Settings
{
public:
    // Per-user directory holding settings.ini + imgui.ini. Resolve-only (does not
    // create it). Empty if neither APPDATA nor HOME is resolvable.
    static std::string ConfigDir();
    // ConfigDir(), creating it (and parents) if missing. Empty on failure.
    static std::string EnsureConfigDir();
    // Full path to settings.ini within ConfigDir().
    static std::string FilePath();
    // Full path to the relocated ImGui layout file (imgui.ini) within ConfigDir().
    // Keeps all config-dir path assembly (and the OS separator) in this module.
    static std::string LayoutFilePath();

    void Load();        // read FilePath(); silent no-op if it doesn't exist
    bool Save() const;  // write FilePath(), creating the config dir first

    bool Has(const std::string& section, const std::string& key) const;
    std::string Get(const std::string& section, const std::string& key,
                    const std::string& def = std::string()) const;
    void Set(const std::string& section, const std::string& key, const std::string& value);
    bool GetBool(const std::string& section, const std::string& key, bool def) const;
    void SetBool(const std::string& section, const std::string& key, bool value);

private:
    // section -> (key -> value). Sections/keys are stored lowercased so lookups
    // are case-insensitive and match whatever casing the installer writes.
    std::map<std::string, std::map<std::string, std::string>> mData;
};

}  // namespace sfe
