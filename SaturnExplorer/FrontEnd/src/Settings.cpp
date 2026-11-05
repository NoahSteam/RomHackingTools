#include "Settings.h"

#include "FileRead.h"
#include "FileWrite.h"
#include "AddressNameLine.h"   // SplitLines

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>
#include <vector>

#ifdef _WIN32
#include <direct.h>
#define SE_MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define SE_MKDIR(p) ::mkdir((p), 0755)
#endif

namespace sfe
{

namespace
{

// settings.ini is a few kilobytes; 4 MiB is a thousand times that.
const size_t kMaxSettingsBytes = 4u * 1024u * 1024u;

#ifdef _WIN32
const char kSep = '\\';
#else
const char kSep = '/';
#endif

// Join a config-dir path with a filename, or empty if the dir is empty. Keeps the
// separator choice in one place for FilePath / LayoutFilePath / Save.
std::string JoinConfig(const std::string& dir, const char* file)
{
    return dir.empty() ? std::string() : dir + kSep + file;
}

std::string Lower(std::string s)
{
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string Trim(const std::string& s)
{
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

// Create `dir` and any missing parents. Accepts '/' or '\\' separators.
bool CreateDirs(const std::string& dir)
{
    std::string p;
    for (size_t i = 0; i <= dir.size(); ++i)
    {
        const char c = (i < dir.size()) ? dir[i] : '\0';
        if (c == '/' || c == '\\' || c == '\0')
        {
            // Don't try to mkdir a drive root ("C:") or an empty leading segment.
            if (p.size() > 1 && !(p.size() == 2 && p[1] == ':'))
            {
                SE_MKDIR(p.c_str());   // ignore EEXIST; a real failure surfaces on write
            }
            if (c != '\0') p.push_back(c);
        }
        else
        {
            p.push_back(c);
        }
    }
    return true;
}

}  // namespace

std::string Settings::ConfigDir()
{
#ifdef _WIN32
    const char* base = std::getenv("APPDATA");
#else
    const char* base = std::getenv("XDG_CONFIG_HOME");
    std::string fallback;
    if (!base || !*base)
    {
        const char* home = std::getenv("HOME");
        if (!home || !*home) return std::string();
        fallback = std::string(home) + "/.config";
        base = fallback.c_str();
    }
#endif
    if (!base || !*base) return std::string();
    return std::string(base) + kSep + "SaturnExplorer";
}

std::string Settings::EnsureConfigDir()
{
    std::string dir = ConfigDir();
    if (!dir.empty()) CreateDirs(dir);
    return dir;
}

std::string Settings::ConfigSubDir(const char* subdir, bool create)
{
    const std::string dir = JoinConfig(create ? EnsureConfigDir() : ConfigDir(), subdir);
    if (create && !dir.empty()) CreateDirs(dir);
    return dir;
}

std::string Settings::ConfigSubPath(const char* subdir, const std::string& file, bool create)
{
    const std::string dir = ConfigSubDir(subdir, create);
    return dir.empty() ? std::string() : dir + kSep + file;
}

bool Settings::EnsureDirectory(const std::string& dir)
{
    if (dir.empty()) return false;
    return CreateDirs(dir);
}

char Settings::PathSeparator()
{
    return kSep;
}

std::string Settings::FilePath()
{
    return JoinConfig(ConfigDir(), "settings.ini");
}

std::string Settings::LayoutFilePath()
{
    return JoinConfig(ConfigDir(), "imgui.ini");
}

namespace
{
using IniData = std::map<std::string, std::map<std::string, std::string>>;

// The INI text into 'data'. A line ends at LF, CRLF or a lone CR (A9-25).
void ParseIni(const std::string& text, IniData& data)
{
    std::string section;
    for (const std::string& line : SplitLines(text))
    {
        std::string t = Trim(line);
        if (t.empty() || t[0] == ';' || t[0] == '#') continue;
        if (t.front() == '[' && t.back() == ']')
        {
            section = Lower(Trim(t.substr(1, t.size() - 2)));
            continue;
        }
        const size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = Lower(Trim(t.substr(0, eq)));
        const std::string val = Trim(t.substr(eq + 1));
        if (!key.empty()) data[section][key] = val;
    }
}

// Why a read of the settings file gave nothing: "" for Ok and Missing.
std::string WhyNotRead(CappedRead r, const std::string& path)
{
    switch (r)
    {
    case CappedRead::TooLarge: return path + " is larger than " + std::to_string(kMaxSettingsBytes >> 20) + " MiB; not read";
    case CappedRead::NotRegular: return path + " is not a regular file; not read";
    case CappedRead::Unreadable: return "could not read " + path;
    default: return std::string();
    }
}
}  // namespace

bool Settings::Load(std::string* error)
{
    const std::string path = FilePath();
    if (path.empty()) return true;
    std::string text;
    const CappedRead r = ReadFileCapped(path, kMaxSettingsBytes, text);
    mLoadedPath = path;
    const std::string why = WhyNotRead(r, path);
    if (!why.empty())
    {
        mRefusedWhy = path + " was refused (" + why + ")";   // no save may replace it (A9-19)
        if (error) *error = why;
        return false;
    }
    mRefusedWhy.clear();
    mBase.clear();
    if (r == CappedRead::Missing) return true;   // first run
    ParseIni(text, mData);
    ParseIni(text, mBase);
    return true;
}

bool Settings::Save(std::string* error) const
{
    // Resolve the dir once (creating it), then derive the file path from it, rather
    // than re-resolving ConfigDir via FilePath().
    const std::string path = JoinConfig(EnsureConfigDir(), "settings.ini");
    if (path.empty())
    {
        if (error) *error = "The settings folder could not be found or created.";
        return false;
    }
    return SaveTo(path, error);
}

bool Settings::SaveTo(const std::string& path, std::string* error) const
{
    auto fail = [&](const std::string& why) { if (error) *error = why; return false; };
    const bool same = path == mLoadedPath;
    static const IniData kNone;
    const IniData& base = same ? mBase : kNone;
    // The keys this session set or changed.
    IniData changes;
    for (const auto& sec : mData)
        for (const auto& kv : sec.second)
        {
            auto s = base.find(sec.first);
            if (s == base.end() || !s->second.count(kv.first) || s->second.at(kv.first) != kv.second) changes[sec.first][kv.first] = kv.second;
        }
    if (changes.empty()) return true;   // an instance that changed nothing writes nothing (A9-20)
    if (same && !mRefusedWhy.empty())
        return fail("Settings: not saved, " + mRefusedWhy + " when it was loaded, and a save would replace it; this session's changes are kept in memory only");
    // The lock file lives beside the data: a lock on the file itself would be dropped by the rename that replaces it.
    FileLock lock(path + ".lock");
    if (!lock.Held())
        return fail("Settings: not saved, " + path + ".lock cannot be opened or locked (a lock file made by another user, a folder in its place, or a refused lock); "
                    "saving without it could lose another instance's keys, so the file is left as it is and this session's changes are kept in memory only");
    RemoveStaleStagingFiles(path);
    std::string disk;
    const CappedRead r = ReadFileCapped(path, kMaxSettingsBytes, disk);
    const std::string why0 = WhyNotRead(r, path);
    if (!why0.empty()) return fail("Settings: not saved, " + why0 + " now; the file is left as it is");
    if (r != CappedRead::Ok) disk.clear();
    // One line per setting. A value with a line feed in it (a path can have one) would end its line and begin another
    // that the next start reads as a setting of its own: a BIOS path could set the volatile list (A9-12). Each line
    // end character (LF, and CR, which the loader now also ends a line at) is written as a space, so what comes back is a
    // value that fails visibly (a path that does not exist), not a different setting.
    auto oneLine = [](std::string v) {
        for (char& c : v) if (c == '\n' || c == '\r') c = ' ';
        return v;
    };
    // The file as it is now, line by line: what this session did not change is kept as written (a hand-written comment, the order
    // of sections and keys, a line that is no setting, A9-34); a changed key is rewritten where it stands; a new key goes after the
    // last key of its section; a new section goes at the end.
    std::vector<std::string> out;
    std::set<std::pair<std::string, std::string>> written;
    std::set<std::string> seen;
    std::string section;
    size_t insertAt = 0;
    seen.insert(section);
    auto flush = [&](const std::string& sec, size_t at) {
        auto c = changes.find(sec);
        if (c == changes.end()) return;
        std::vector<std::string> add;
        for (const auto& kv : c->second)
            if (written.insert({ sec, kv.first }).second) add.push_back(oneLine(kv.first) + " = " + oneLine(kv.second));
        out.insert(out.begin() + static_cast<std::ptrdiff_t>(at), add.begin(), add.end());
    };
    for (const std::string& line : SplitLines(disk))
    {
        const std::string t = Trim(line);
        if (!t.empty() && t[0] != ';' && t[0] != '#')
        {
            if (t.front() == '[' && t.back() == ']')
            {
                flush(section, insertAt);
                section = Lower(Trim(t.substr(1, t.size() - 2)));
                seen.insert(section);
                out.push_back(line);
                insertAt = out.size();
                continue;
            }
            const size_t eq = t.find('=');
            if (eq != std::string::npos)
            {
                const std::string keyText = Trim(t.substr(0, eq));
                const std::string key = Lower(keyText);
                if (!key.empty())
                {
                    auto c = changes.find(section);
                    if (c != changes.end() && c->second.count(key))
                    {
                        out.push_back(oneLine(keyText) + " = " + oneLine(c->second.at(key)));
                        written.insert({ section, key });
                    }
                    else out.push_back(line);
                    insertAt = out.size();
                    continue;
                }
            }
        }
        out.push_back(line);
    }
    flush(section, insertAt);
    for (const auto& sec : changes)
    {
        if (seen.count(sec.first)) continue;
        if (!out.empty() && !Trim(out.back()).empty()) out.push_back(std::string());
        out.push_back('[' + oneLine(sec.first) + ']');
        for (const auto& kv : sec.second) out.push_back(oneLine(kv.first) + " = " + oneLine(kv.second));
    }
    std::string text;
    for (const std::string& l : out) { text += l; text += '\n'; }
    if (text.size() > kMaxSettingsBytes) return fail("Settings: not saved, it would be over the 4 MiB that is read back at the next start");
    // Not an ofstream: it truncates the file on open and flushes in its destructor, after any check the
    // caller could make, so a failed write both lost the old settings and reported success.
    std::string why;
    if (!WriteFileAtomically(path, text.data(), text.size(), why)) return fail(path + ": " + why);
    if (same) mBase = mData;
    return true;
}

bool Settings::Has(const std::string& section, const std::string& key) const
{
    auto s = mData.find(Lower(section));
    if (s == mData.end()) return false;
    return s->second.find(Lower(key)) != s->second.end();
}

std::string Settings::Get(const std::string& section, const std::string& key,
                          const std::string& def) const
{
    auto s = mData.find(Lower(section));
    if (s == mData.end()) return def;
    auto k = s->second.find(Lower(key));
    return (k == s->second.end()) ? def : k->second;
}

void Settings::Set(const std::string& section, const std::string& key, const std::string& value)
{
    mData[Lower(section)][Lower(key)] = value;
}

bool Settings::GetBool(const std::string& section, const std::string& key, bool def) const
{
    if (!Has(section, key)) return def;
    const std::string v = Lower(Get(section, key));
    return v == "1" || v == "true" || v == "yes" || v == "on";
}

void Settings::SetBool(const std::string& section, const std::string& key, bool value)
{
    Set(section, key, value ? "1" : "0");
}

float Settings::GetFloat(const std::string& section, const std::string& key, float def) const
{
    // A missing key, or anything a hand-edited INI holds that isn't purely a number,
    // leaves the default rather than a 0 — for a size those mean very different things.
    const std::string s = Trim(Get(section, key));
    char* end = nullptr;
    const float v = std::strtof(s.c_str(), &end);
    // "nan" and "inf" are numbers to strtof and a layout size that breaks every frame after it (A9-13).
    return (!s.empty() && end == s.c_str() + s.size() && std::isfinite(v)) ? v : def;
}

void Settings::SetFloat(const std::string& section, const std::string& key, float value)
{
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.1f", value);
    Set(section, key, buf);
}

}  // namespace sfe
