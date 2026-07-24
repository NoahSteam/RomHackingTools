// Launcher — the "Launch Session" model: which emulator + which ROM the debugger
// starts, plus how each emulator takes a ROM on its command line. The user isn't
// really choosing an executable each time — they're choosing a debugging environment
// (emulator + game), so this holds that selection and persists it.
//
// Pure data + logic: no ImGui, no platform calls. The App owns one Launcher, draws the
// nested Launch menu / Launch Settings dialog around it, and hands the resolved
// (exe, args, workdir) to IPlatform::LaunchProcess. That split keeps the arg templating,
// recent-list, and load/save logic unit-testable.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace sfe
{

class Settings;

// One launchable emulator: an executable + how to pass a ROM to it. `argsTemplate`
// contains the token {rom}, replaced with the selected ROM path at launch — keep the
// surrounding quotes in the template ("{rom}") so paths with spaces survive. Known
// emulators ship sensible defaults; exe/args/workdir are user-editable (Launch
// Settings) and persisted.
struct EmulatorSpec
{
    std::string key;           // stable id; matches the installer's [emulators] key ("mednafen")
    std::string label;         // display name ("Mednafen")
    std::string exePath;       // full path to the executable ("" if not installed yet)
    std::string argsTemplate;  // e.g. "\"{rom}\"" (Mednafen) or "-a -i \"{rom}\"" (Yabause)
    std::string workDir;       // launch working directory ("" = the exe's own folder)
    std::string biosPath;      // optional Saturn BIOS image; substituted for {bios} in args
};

struct LaunchValidation
{
    bool        valid = false;
    std::string message;
};

// Substitute the {rom} and {bios} tokens in `argsTemplate`. If `rom` is empty the whole
// template collapses to "" — launch bare (e.g. boot to the emulator's own menu), never
// passing a stray empty "" or dangling flag. Otherwise {rom}->rom and {bios}->bios (an
// empty bios substitutes to nothing); runs of whitespace left behind are collapsed so an
// unused {bios} doesn't leave a double space.
std::string BuildLaunchArgs(const std::string& argsTemplate, const std::string& rom,
                            const std::string& bios = std::string());

// Basename of a path (component after the last '/' or '\\'), for display.
std::string PathBasename(const std::string& path);

// The launcher's persisted state: the known emulators (with per-emulator overrides),
// which emulator + ROM are selected, a most-recently-used ROM list, and whether
// launching should also use the ROM's containing folder as the Data Directory.
// Unit-testable.
class Launcher
{
public:
    static constexpr std::size_t kMaxRecent = 8;

    // Populate the known emulators (built-in defaults), overlay any saved overrides
    // (exe path, args template, working dir), and restore the selection + recent list.
    void Load(const Settings& s);
    // Persist emulator overrides, the current selection, the recent list, and the
    // set-data-dir coupling back into settings.
    void Save(Settings& s) const;

    const std::vector<EmulatorSpec>& Emulators() const { return mEmus; }
    std::vector<EmulatorSpec>&       Emulators()       { return mEmus; }

    int  SelectedIndex() const { return mSel; }
    void Select(int i);
    const EmulatorSpec* Selected() const;   // nullptr if the index is out of range

    const std::string& Rom() const { return mRom; }
    void SetRom(const std::string& rom);    // sets current + promotes it in the recent list
    void ClearRom() { mRom.clear(); }

    const std::vector<std::string>& Recent() const { return mRecent; }
    void ClearRecent() { mRecent.clear(); }

    bool SetDataDirOnLaunch() const { return mSetDataDir; }
    void SetSetDataDirOnLaunch(bool v) { mSetDataDir = v; }

    // The resolved argument string for the selected emulator + current ROM (empty if
    // no emulator is selected). What the App passes to LaunchProcess.
    std::string CurrentArgs() const;

    // Validate the complete configuration used by the primary Launch action.
    // This deliberately lives in the model so the toolbar, menu, and launch
    // command all use the same rules.
    LaunchValidation Validate() const;

private:
    std::vector<EmulatorSpec> mEmus;
    int                       mSel = 0;
    std::string               mRom;
    std::vector<std::string>  mRecent;
    bool                      mSetDataDir = true;
};

}  // namespace sfe
