#include "Launcher.h"

#include <algorithm>
#include <cstdint>
#include <fstream>

#ifdef _WIN32
#include <direct.h>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <limits.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif

#include "Settings.h"
#include "DataSearch.h"
#include "ArgSplit.h"

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

bool FileExists(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    return static_cast<bool>(f);
}

std::string DirectoryOf(const std::string& path)
{
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

std::string JoinPath(const std::string& dir, const char* leaf)
{
    if (dir.empty()) return std::string();
    const char last = dir.back();
    return dir + ((last == '/' || last == '\\') ? "" :
#ifdef _WIN32
                  "\\"
#else
                  "/"
#endif
                  ) + leaf;
}

// Builds before the portable layout fix were recorded as <checkout>/src/mednafen.exe.
// Prefer the published root copy when it exists, without disturbing custom paths.
std::string MigrateMednafenExe(const std::string& path)
{
    const size_t slash = path.find_last_of("/\\");
    if (slash == std::string::npos || PathBasename(path) != "mednafen.exe") return path;
    const std::string dir = path.substr(0, slash);
    if (PathBasename(dir) != "src") return path;
    const size_t parentSlash = dir.find_last_of("/\\");
    if (parentSlash == std::string::npos) return path;
    const std::string candidate = dir.substr(0, parentSlash + 1) + "mednafen.exe";
    return FileExists(candidate) ? candidate : path;
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

std::vector<std::string> BuildLaunchArgv(const std::string& argsTemplate, const std::string& rom,
                                         const std::string& bios)
{
    std::vector<std::string> out;
    if (rom.empty()) return out;   // no ROM -> launch bare
    const std::vector<std::pair<std::string, std::string>> tokens = { { kRomToken, rom }, { kBiosToken, bios } };
    for (const std::string& arg : SplitCommandLine(argsTemplate))
    {
        bool touched = false;
        std::string value = SubstituteTokens(arg, tokens, &touched);
        if (touched && value.empty()) continue;   // "{bios}" with no BIOS
        out.push_back(std::move(value));
    }
    return out;
}

std::string AbsolutePath(const std::string& path)
{
    if (path.empty()) return path;
#ifdef _WIN32
    // Windows has more shapes than "starts with a separator": "\\x" is rooted on the CURRENT drive and
    // "C:x" is relative to the current directory ON drive C. The OS resolver knows them all.
    char full[4096];
    return _fullpath(full, path.c_str(), sizeof(full)) ? std::string(full) : path;
#else
    // Only a leading '/' is absolute. A backslash is an ordinary filename character here, so "\\disc.cue"
    // and "C:disc.cue" are relative names and belong under the current directory.
    if (path[0] == '/') return path;
    char cwd[4096];
    if (!getcwd(cwd, sizeof(cwd))) return path;
    return std::string(cwd) + "/" + path;
#endif
}

std::string ExecutablePath()
{
#if defined(_WIN32)
    // The ANSI form, on purpose: every file and process call this program makes (ifstream, ShellExecuteExA)
    // takes narrow paths in the active code page, so a UTF-8 string here would name a different file for
    // any non-ASCII folder.
    char buf[MAX_PATH * 2];
    const DWORD n = GetModuleFileNameA(nullptr, buf, static_cast<DWORD>(sizeof(buf)));
    return (n == 0 || n >= sizeof(buf)) ? std::string() : std::string(buf, n);
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string out(size, '\0');
    if (_NSGetExecutablePath(&out[0], &size) != 0) return std::string();
    out.resize(out.find('\0'));
    char real[PATH_MAX];
    return ::realpath(out.c_str(), real) ? std::string(real) : out;
#else
    char buf[PATH_MAX];
    const ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    return n > 0 ? std::string(buf, static_cast<size_t>(n)) : std::string();
#endif
}

std::string BundledMednafenPath(const std::string& executablePath)
{
    std::string root = DirectoryOf(executablePath);
    // Inside a macOS bundle the program is Foo.app/Contents/MacOS/Foo; the release puts mednafen-se/
    // beside the .app, because the bundle is sealed by its signature and the user must be able to add
    // the firmware folder to the emulator's directory. Only the bundle the program itself sits in counts:
    // its folder must END in .app/Contents/MacOS, so an .app somewhere further up the path is not mistaken
    // for it.
    static const std::string kInBundle = ".app/Contents/MacOS";
    if (root.size() > kInBundle.size() &&
        root.compare(root.size() - kInBundle.size(), kInBundle.size(), kInBundle) == 0)
        root = DirectoryOf(root.substr(0, root.size() - kInBundle.size() + 4));   // keep ".app", then step out of it
    if (root.empty()) return std::string();
#ifdef _WIN32
    const char* leaf = "mednafen.exe";
#else
    const char* leaf = "mednafen";
#endif
    const std::string candidate = JoinPath(JoinPath(root, "mednafen-se"), leaf);
    return FileExists(candidate) ? candidate : std::string();
}

std::string MednafenFirmwareDir(const std::string& exePath)
{
    return JoinPath(DirectoryOf(exePath), "firmware");
}

std::string PathBasename(const std::string& path)
{
    const size_t slash = path.find_last_of("/\\");
    return (slash == std::string::npos) ? path : path.substr(slash + 1);
}

void Launcher::Load(const Settings& s, const std::string& executablePath)
{
    mBundledMednafen = BundledMednafenPath(executablePath);
    mEmus.clear();
    for (const Known& k : kKnown)
    {
        EmulatorSpec e;
        e.key   = k.key;
        e.label = k.label;
        // Exe path is written by the installer under [emulators] <key>; Launch Settings
        // edits that same key so an override persists there.
        e.exePath      = s.Get("emulators", k.key, "");
        if (e.key == "mednafen")
        {
            e.exePath = MigrateMednafenExe(e.exePath);
            // A release ships its own patched Mednafen beside the program: with no path recorded, use it.
            // Save() leaves the path empty while the bundled copy is in use, so a download that is moved
            // keeps finding the copy beside it, and a path the user did record -- even one that is missing
            // right now, on an unmounted volume -- is never replaced.
            if (e.exePath.empty()) e.exePath = mBundledMednafen;
        }
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
        // keep the installer's key in sync with overrides; the bundled copy is found again at every start
        const bool bundled = e.key == "mednafen" && !mBundledMednafen.empty() && e.exePath == mBundledMednafen;
        s.Set("emulators", e.key, bundled ? std::string() : e.exePath);
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

bool Launcher::UsesBundledMednafen() const
{
    if (mBundledMednafen.empty()) return false;
    for (const EmulatorSpec& e : mEmus)
        if (e.key == "mednafen") return e.exePath == mBundledMednafen;
    return false;
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

LaunchValidation Launcher::Validate() const
{
    LaunchValidation result;
    const EmulatorSpec* e = Selected();
    if (!e)
    {
        result.message = "No emulator is selected.";
        return result;
    }
    if (e->exePath.empty() || !PathExists(e->exePath))
    {
        result.message = e->label + " executable was not found. Open Launch Settings to configure it.";
        return result;
    }
    if (mRom.empty())
    {
        result.message = "Select a game or ROM before launching.";
        return result;
    }
    if (!PathExists(mRom))
    {
        result.message = "The selected game or ROM no longer exists.";
        return result;
    }
    if (!e->workDir.empty() && !IsDirectory(e->workDir))
    {
        result.message = "The configured working directory does not exist.";
        return result;
    }
    if (e->argsTemplate.find("{rom}") == std::string::npos)
    {
        result.message = "The arguments template must contain {rom}.";
        return result;
    }
    if (e->key == "mednafen")
    {
        const std::string firmwareDir = MednafenFirmwareDir(e->exePath);
        if (!PathExists(JoinPath(firmwareDir, "sega_101.bin")))
        {
            result.message = "Missing firmware/sega_101.bin beside Mednafen.";
            return result;
        }
        if (!PathExists(JoinPath(firmwareDir, "mpr-17933.bin")))
        {
            result.message = "Missing firmware/mpr-17933.bin beside Mednafen.";
            return result;
        }
    }
    result.valid = true;
    return result;
}

}  // namespace sfe
