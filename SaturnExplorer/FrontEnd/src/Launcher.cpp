#include "Launcher.h"

#include <algorithm>

#include "Settings.h"

namespace sfe
{
namespace
{
// Built-in emulators and how each takes a Saturn disc on its command line. These are
// the defaults; the user can edit args in Launch Settings if a fork differs.
//   Mednafen : positional filename           -> mednafen.exe "<rom>"
//   Yabause  : -a (autostart) -i <iso>        -> yabause-qt.exe -a -i "<rom>"
//              (shared by the Qt-lineage forks Yaba Sanshiro / Kronos, all recorded
//               under the "yabause" key by the installer)
struct Known
{
    const char* key;
    const char* label;
    const char* args;
};
const Known kKnown[] = {
    {"mednafen", "Mednafen", "\"{rom}\""},
    {"yabause",  "Yabause",  "-a -i \"{rom}\""},
};

const char* kRomToken  = "{rom}";
const char* kBiosToken = "{bios}";

// Replace every occurrence of `tok` in `s` with `val`.
std::string Replace(const std::string& s, const std::string& tok, const std::string& val)
{
    std::string out;
    size_t i = 0, p;
    while ((p = s.find(tok, i)) != std::string::npos)
    {
        out.append(s, i, p - i);
        out += val;
        i = p + tok.size();
    }
    out.append(s, i, std::string::npos);
    return out;
}

// Collapse runs of spaces/tabs to a single space and trim the ends (so an unused {bios}
// that expanded to "" doesn't leave a double space).
std::string CollapseSpaces(const std::string& s)
{
    std::string out;
    bool sp = false;
    for (char c : s)
    {
        if (c == ' ' || c == '\t') { sp = true; continue; }
        if (sp && !out.empty()) out += ' ';
        sp = false;
        out += c;
    }
    return out;
}
}  // namespace

std::string BuildLaunchArgs(const std::string& argsTemplate, const std::string& rom,
                            const std::string& bios)
{
    if (rom.empty()) return std::string();   // no ROM -> launch bare
    // Substitute {bios} first (empty -> vanishes), tidy whitespace, THEN {rom} last so a
    // ROM path containing spaces is never touched by the whitespace collapse.
    std::string out = Replace(argsTemplate, kBiosToken, bios);
    out = CollapseSpaces(out);
    out = Replace(out, kRomToken, rom);
    return out;
}

std::string PathBasename(const std::string& path)
{
    const size_t slash = path.find_last_of("/\\");
    return (slash == std::string::npos) ? path : path.substr(slash + 1);
}

void Launcher::Load(const Settings& s)
{
    mEmus.clear();
    for (const Known& k : kKnown)
    {
        EmulatorSpec e;
        e.key   = k.key;
        e.label = k.label;
        // Exe path is written by the installer under [emulators] <key>; Launch Settings
        // edits that same key so an override persists there.
        e.exePath      = s.Get("emulators", k.key, "");
        const std::string sect = std::string("launch.") + k.key;
        e.argsTemplate = s.Get(sect, "args", k.args);
        e.workDir      = s.Get(sect, "workdir", "");
        e.biosPath     = s.Get(sect, "bios", "");
        mEmus.push_back(std::move(e));
    }

    // Restore the selected emulator by key (fall back to the first).
    mSel = 0;
    const std::string selKey = s.Get("launch", "emulator", "");
    for (size_t i = 0; i < mEmus.size(); ++i)
        if (mEmus[i].key == selKey) { mSel = static_cast<int>(i); break; }

    mRom = s.Get("launch", "rom", "");

    // Recent list: fixed slots recent0..recent{kMaxRecent-1}; collect the non-empty ones
    // in order (skipping blanks so a shrunk list leaves no stale tail).
    mRecent.clear();
    for (size_t i = 0; i < kMaxRecent; ++i)
    {
        const std::string v = s.Get("launch", "recent" + std::to_string(i), "");
        if (!v.empty()) mRecent.push_back(v);
    }

    mSetDataDir = s.GetBool("launch", "setdatadir", true);
}

void Launcher::Save(Settings& s) const
{
    for (const EmulatorSpec& e : mEmus)
    {
        s.Set("emulators", e.key, e.exePath);   // keep the installer's key in sync with overrides
        const std::string sect = std::string("launch.") + e.key;
        s.Set(sect, "args", e.argsTemplate);
        s.Set(sect, "workdir", e.workDir);
        s.Set(sect, "bios", e.biosPath);
    }
    s.Set("launch", "emulator", (mSel >= 0 && mSel < static_cast<int>(mEmus.size()))
                                    ? mEmus[mSel].key : std::string());
    s.Set("launch", "rom", mRom);
    // Write every slot (blanking unused ones) so a shrunk recent list clears its tail.
    for (size_t i = 0; i < kMaxRecent; ++i)
        s.Set("launch", "recent" + std::to_string(i),
              i < mRecent.size() ? mRecent[i] : std::string());
    s.SetBool("launch", "setdatadir", mSetDataDir);
}

void Launcher::Select(int i)
{
    if (i >= 0 && i < static_cast<int>(mEmus.size())) mSel = i;
}

const EmulatorSpec* Launcher::Selected() const
{
    if (mSel < 0 || mSel >= static_cast<int>(mEmus.size())) return nullptr;
    return &mEmus[mSel];
}

void Launcher::SetRom(const std::string& rom)
{
    mRom = rom;
    if (rom.empty()) return;
    // Move-to-front, de-duplicated, capped: most-recently-used ordering.
    mRecent.erase(std::remove(mRecent.begin(), mRecent.end(), rom), mRecent.end());
    mRecent.insert(mRecent.begin(), rom);
    if (mRecent.size() > kMaxRecent) mRecent.resize(kMaxRecent);
}

std::string Launcher::CurrentArgs() const
{
    const EmulatorSpec* e = Selected();
    return e ? BuildLaunchArgs(e->argsTemplate, mRom, e->biosPath) : std::string();
}

}  // namespace sfe
